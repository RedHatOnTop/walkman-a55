/*
 * fbcap [OUTFILE]
 *
 * Captures what the MTK overlay engine sends to the panel and writes it raw.
 *
 * Reading /dev/fb0's memory is not ground truth on this SoC: SurfaceFlinger and
 * Sony's UI compose through the OVL engine, so the framebuffer may not hold the
 * composed image. MTKFB_CAPTURE_FRAMEBUFFER has the overlay engine write its
 * composed output into a user buffer instead.
 *
 * Source for the whole path (kernel 3.10.26, oss/linux-kernel-3.10.26.tar.gz):
 *  - drivers/misc/mediatek/videox/mt8590/mtkfb.h:43 defines
 *    MTKFB_CAPTURE_FRAMEBUFFER as MTK_IOW(3, unsigned long), i.e.
 *    _IOW('O', 3, 4) = 0x40044f03.
 *  - mtkfb.c:2680 (the ioctl switch) copy_from_user()s a 4-byte user pointer
 *    out of the argument and passes it to mtkfb_capture_framebuffer.
 *  - mtkfb.c:2152 calls DISP_Capture_Framebuffer(pvbuf, bpp, is_early_suspended)
 *    for the non-UI-layer case.
 *  - disp_drv.c:1803 maps that buffer through M4U for
 *    DISP_GetScreenWidth() * DISP_GetScreenHeight() * bpp / 8 bytes and has the
 *    overlay engine write the composed output there. disp_drv.c:1826-1830
 *    zeroes the buffer instead when an enabled layer is marked secure or the
 *    display is early-suspended, so an all-zero capture means "not available",
 *    not "black screen".
 *
 * The buffer is written by hardware, so it has to stay resident: every page is
 * touched and the mapping is mlocked. The geometry comes from
 * FBIOGET_VSCREENINFO, and the kernel logs every mtkfb ioctl with its decoded
 * name, so dmesg confirms which ioctl ran.
 *
 * Read-only: the worst case is a kernel oops and a reboot.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* MTK_IOW('O', 3, unsigned long) on ARM. */
#define MTKFB_CAPTURE_FRAMEBUFFER 0x40044f03UL

static int write_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *) buf;

    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        p += n;
        len -= (size_t) n;
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct fb_var_screeninfo var;
    const char *paths[] = { "/dev/graphics/fb0", "/dev/fb0", NULL };
    const char *out_path = (argc > 1) ? argv[1] : NULL;
    unsigned int addr;
    unsigned char *buf;
    size_t size;
    long page;
    size_t off;
    int fd = -1;
    int out;
    int i;

    for (i = 0; paths[i] != NULL; i++) {
        fd = open(paths[i], O_RDWR);
        if (fd >= 0) {
            fprintf(stderr, "fbcap: opened %s\n", paths[i]);
            break;
        }
    }
    if (fd < 0) {
        perror("fbcap: open");
        return 1;
    }

    memset(&var, 0, sizeof(var));
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
        perror("fbcap: FBIOGET_VSCREENINFO");
        close(fd);
        return 1;
    }
    if (var.bits_per_pixel != 32 && var.bits_per_pixel != 16) {
        fprintf(stderr, "fbcap: unexpected bpp %u\n", var.bits_per_pixel);
        close(fd);
        return 1;
    }

    size = (size_t) var.xres * var.yres * (var.bits_per_pixel / 8);
    fprintf(stderr, "fbcap: %ux%u %ubpp, %zu bytes\n",
            var.xres, var.yres, var.bits_per_pixel, size);

    page = sysconf(_SC_PAGESIZE);
    if (page <= 0) {
        page = 4096;
    }
    if (posix_memalign((void **) &buf, (size_t) page, size) != 0) {
        perror("fbcap: posix_memalign");
        close(fd);
        return 1;
    }
    /* The hardware writes here, so the pages must be resident. */
    memset(buf, 0, size);
    if (mlock(buf, size) != 0) {
        fprintf(stderr, "fbcap: mlock: %s (continuing)\n", strerror(errno));
    }

    addr = (unsigned int) (unsigned long) buf;
    if (ioctl(fd, MTKFB_CAPTURE_FRAMEBUFFER, &addr) < 0) {
        perror("fbcap: MTKFB_CAPTURE_FRAMEBUFFER");
        munlock(buf, size);
        free(buf);
        close(fd);
        return 1;
    }

    off = 0;
    for (off = 0; off < size; off += (size_t) page) {
        /* Touch again so the capture's writes are visible to us. */
        buf[off] = buf[off];
    }

    out = (out_path != NULL) ? open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644)
                             : STDOUT_FILENO;
    if (out < 0) {
        perror(out_path);
        munlock(buf, size);
        free(buf);
        close(fd);
        return 1;
    }
    if (write_all(out, buf, size) != 0) {
        perror("fbcap: write");
        if (out != STDOUT_FILENO) {
            close(out);
        }
        munlock(buf, size);
        free(buf);
        close(fd);
        return 1;
    }
    if (out != STDOUT_FILENO) {
        close(out);
    }

    munlock(buf, size);
    free(buf);
    close(fd);
    return 0;
}
