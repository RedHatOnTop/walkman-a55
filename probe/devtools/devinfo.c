/*
 * The SoC's efuse words as the kernel's devinfo driver hands them out, and what
 * the MT8590 kernel derives from word 3: how many Cortex-A7 cores the die has
 * enabled (mt-smp.c L2CTLR_get_core_count) and how many Mali-450 pixel
 * processors (gpu/mali/mt8590 platform.c update_dev_info). The stock kernel
 * ignores both: NR_CPUS=2, and the Mali driver is built with MTK_CONFIG_FF_PP,
 * which pins MP1. So these say what a rebuilt kernel could use, not what runs.
 *
 *   devinfo            decode word 3, dump words 0..31
 *   devinfo N          print word N
 *
 * Interface from drivers/misc/mediatek/devinfo/mt8590/devinfo.h. The node is
 * created by the driver's class, but /dev here is a plain tmpfs, so make it
 * from /proc/devices when it is missing.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#define READ_DEV_DATA _IOR('d', 1, unsigned int)
#define NODE "/dev/devmap"

static int major_of(const char *name)
{
    char line[128];
    int major = -1;
    FILE *f = fopen("/proc/devices", "r");
    if (!f)
        return -1;
    while (fgets(line, sizeof line, f)) {
        int m;
        char n[64];
        if (sscanf(line, "%d %63s", &m, n) == 2 && !strcmp(n, name)) {
            major = m;
            break;
        }
    }
    fclose(f);
    return major;
}

static int open_node(void)
{
    int fd = open(NODE, O_RDONLY);
    if (fd >= 0 || errno != ENOENT)
        return fd;
    int major = major_of("devmap");
    if (major < 0) {
        fprintf(stderr, "no devmap in /proc/devices\n");
        return -1;
    }
    if (mknod(NODE, S_IFCHR | 0600, makedev(major, 0)) < 0) {
        perror("mknod " NODE);
        return -1;
    }
    return open(NODE, O_RDONLY);
}

static int word(int fd, unsigned int index, unsigned int *value)
{
    *value = index;
    return ioctl(fd, READ_DEV_DATA, value);
}

int main(int argc, char **argv)
{
    unsigned int v, i;
    int fd = open_node();
    if (fd < 0) {
        perror(NODE);
        return 1;
    }
    if (argc > 1) {
        unsigned int index = strtoul(argv[1], NULL, 0);
        if (word(fd, index, &v) < 0) {
            perror("READ_DEV_DATA");
            return 1;
        }
        printf("%u 0x%08x\n", index, v);
        return 0;
    }
    if (word(fd, 3, &v) < 0) {
        perror("READ_DEV_DATA 3");
        return 1;
    }
    unsigned int cpu = (v >> 24) & 0xf, pp = (v & 0x60000) >> 17;
    printf("word3 0x%08x\n", v);
    printf("cpu fuse %#x -> %d cores\n", cpu, cpu == 0 ? 4 : cpu == 8 ? 3 : 2);
    printf("gpu fuse %u -> Mali-450 MP%d\n", pp, pp == 0 ? 4 : 4 - (int)pp);
    for (i = 0; i < 32; i++) {
        if (word(fd, i, &v) < 0)
            break;
        printf("%2u 0x%08x\n", i, v);
    }
    return 0;
}
