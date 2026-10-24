/*
 * runas UID GID PROG [ARGS...]: drops to UID/GID and execs PROG. Static glibc,
 * so it works inside the Fire chroot; an NDK static build does not, since its
 * bionic refuses the TLS alignment. servicemanager needs it: the kernel's binder
 * remembers the euid of the first context manager since boot, which on this
 * device is Sony's system user, uid 100.
 */
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: runas UID GID PROG [ARGS...]\n");
        return 2;
    }
    gid_t gid;

    gid = (gid_t)atoi(argv[2]);
    if (setgroups(1, &gid) || setgid(gid) || setuid((uid_t)atoi(argv[1]))) {
        perror("runas");
        return 1;
    }
    execv(argv[3], argv + 3);
    perror(argv[3]);
    return 1;
}
