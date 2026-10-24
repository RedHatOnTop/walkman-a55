/*
 * pivotinit ROOT [PROG ARGS...]
 * pivotinit --join PID PROG [ARGS...]
 *
 * Starts Android's init with ROOT as the root of a private mount namespace, and
 * joins that namespace again for debugging.
 *
 * Why the container root has to be the namespace root: Amazon's zygote builds a
 * file descriptor table before forking system_server and aborts on any inherited
 * descriptor whose path is not on its whitelist. The property-area descriptor
 * that init dup'd into zygote belongs to a mount of the namespace init was in;
 * zygote ends up elsewhere (ART's Runtime::InitZygote unshares a mount
 * namespace, which is where the earlier "Failed to mount() rootfs as MS_SLAVE"
 * comes from), so this kernel's d_path cannot reach it from zygote's root and
 * falls back to the absolute path:
 *
 *   E/Zygote: Not whitelisted : /tmp/aroot/dev/__properties__
 *   JNI FatalError ... com_android_internal_os_Zygote.cpp:482:
 *       Unable to construct file descriptor table.
 *
 * With the container root mounted over /, that descriptor resolves as
 * /dev/__properties__ and the whitelist accepts it.
 *
 * Everything above stderr is closed before exec as well: adbd hands down Sony
 * init's property-area descriptor, which is unlinked and can therefore never be
 * whitelisted either.
 *
 * ROOT must already be a mount point. The device script binds it onto itself,
 * which ART's MS_SLAVE mount also needs.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef MS_REC
#define MS_REC 16384
#endif
#ifndef MS_PRIVATE
#define MS_PRIVATE (1 << 18)
#endif
#ifndef SYS_setns
#define SYS_setns 375 /* __NR_setns on ARM EABI */
#endif

/* Leave nothing behind for Android to trip over. */
static void close_extra_fds(void)
{
    struct rlimit rl;
    long max = 1024;
    long fd;

    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY) {
        max = (long) rl.rlim_cur;
    }
    if (max > 65536) {
        max = 65536;
    }
    for (fd = 3; fd < max; fd++) {
        close((int) fd);
    }
}

static int join_namespace(const char *pid)
{
    char path[64];
    int fd;

    snprintf(path, sizeof(path), "/proc/%s/ns/mnt", pid);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return 1;
    }
    /* The crosstool's glibc has a setns() stub that always fails, so call the
     * syscall directly. */
    if (syscall(SYS_setns, fd, CLONE_NEWNS) != 0) {
        perror("setns");
        close(fd);
        return 1;
    }
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 2 && strcmp(argv[1], "--join") == 0) {
        if (argc < 4) {
            fprintf(stderr, "usage: pivotinit --join PID PROG [ARGS...]\n");
            return 2;
        }
        if (join_namespace(argv[2]) != 0) {
            return 1;
        }
        execv(argv[3], argv + 3);
        perror(argv[3]);
        return 1;
    }

    if (argc < 2) {
        fprintf(stderr, "usage: pivotinit ROOT [PROG ARGS...]\n");
        return 2;
    }

    if (unshare(CLONE_NEWNS) != 0) {
        perror("unshare(CLONE_NEWNS)");
        return 1;
    }
    /* Mounts must not be shared for the move below to be namespace-local. */
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) {
        perror("mount(/, MS_REC|MS_PRIVATE)");
        return 1;
    }
    /* pivot_root() is not usable here: Sony's / is a bare rootfs mount, and
     * this kernel's pivot_root rejects a root mount with no parent
     * (mnt_has_parent() in fs/namespace.c). Moving the container root on top of
     * / gets the same result, and chroot() then pins the process root there. */
    if (chdir(argv[1]) != 0) {
        perror(argv[1]);
        return 1;
    }
    if (mount(".", "/", NULL, MS_MOVE, NULL) != 0) {
        perror("mount(MS_MOVE)");
        return 1;
    }
    if (chroot(".") != 0) {
        perror("chroot");
        return 1;
    }
    if (chdir("/") != 0) {
        perror("chdir /");
        return 1;
    }

    /* Record the pid where the device script can find it. It lands in the
     * container root, which is $ROOT on the host; the script then identifies
     * the container's processes by mount namespace, since the root of a
     * container process reads back as "/" rather than $ROOT. */
    {
        int fd = open("/init.pid", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            char buf[16];
            int n = snprintf(buf, sizeof(buf), "%d\n", (int) getpid());
            if (n > 0 && write(fd, buf, (size_t) n) < 0) {
                perror("write /init.pid");
            }
            close(fd);
        }
    }

    close_extra_fds();

    /* Opt-in experiment: drop CAP_SYS_BOOT from the bounding set so nothing
     * inside the container can reboot the shared kernel. This tree's reboot
     * syscall gates on ns_capable(pid_ns->user_ns, CAP_SYS_BOOT)
     * (kernel/sys.c:480-489), so with the capability gone reboot(2) returns
     * EPERM. Dropping a capability only makes the container less able to
     * touch the device. Enabled by PIVOTINIT_DROP_SYS_BOOT in the
     * environment; off by default. */
    if (getenv("PIVOTINIT_DROP_SYS_BOOT") != NULL) {
        if (prctl(PR_CAPBSET_DROP, 22 /* CAP_SYS_BOOT */, 0, 0, 0) != 0) {
            perror("prctl(PR_CAPBSET_DROP, CAP_SYS_BOOT)");
            return 1;
        }
        fprintf(stderr, "pivotinit: dropped CAP_SYS_BOOT from the bounding set\n");
    }

    /* core_pattern is the Sony host's and pipes every crash to hcoredump.sh,
     * which runs hdumpstate and gzips the whole core into /var/log: 7.2 s of
     * CPU in one minute of a Bluetooth crash loop. This kernel (3.10)
     * drops a piped core when the soft RLIMIT_CORE is exactly 1, and a hard
     * limit of 1 keeps ART's setrlimit(0, RLIM_INFINITY) from changing it.
     * debuggerd still writes the container's tombstones. */
    {
        struct rlimit core = { 1, 1 };
        if (setrlimit(RLIMIT_CORE, &core) != 0) {
            perror("setrlimit(RLIMIT_CORE)");
            return 1;
        }
    }

    if (argc > 2) {
        execv(argv[2], argv + 2);
        perror(argv[2]);
        return 1;
    }
    execv("/init", (char *const[]) { "/init", NULL });
    perror("/init");
    return 1;
}
