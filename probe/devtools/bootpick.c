/*
 * bootpick FRAMES DEFAULT SECONDS
 * bootpick -i
 *
 * The dual-boot picker (devlog/2026-10-21-boot-picker.md). It runs from the
 * Sony init's bootswitcher.sh, before any Sony service starts, so it has the
 * panel and the keys to itself. It shows the frames tools/bootpick_frames.py
 * rendered on fb0. The volume keys (and prev/next) move the selection; power
 * or play selects. With no key pressed, DEFAULT (1 Walkman, 2 Android) is
 * taken after SECONDS; the first key stops that countdown, and 0 waits from the
 * start. A minute without a key takes the entry selected then, so a device
 * left at the picker does not keep init and the panel waiting for good.
 *
 * Exit status: 1 Walkman, 2 Android. Anything else (bad frames, no panel)
 * means the caller boots Walkman. -i prints the panel and key devices.
 *
 * fb0 is driven the way Sony's icx_bootanimation drives it (disassembly):
 * yoffset 0, then FBIOPUT_VSCREENINFO with FB_ACTIVATE_FORCE after every draw.
 * The volume and power keys are on mtk-kpd, built into the kernel. Play and
 * prev/next are on icx_key, which wm_key.ko creates once load_sony_driver,
 * started just before, gets there, so the devices are scanned again until it
 * shows up.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define FB_PATH "/dev/graphics/fb0"
#define TICK_MS 50
#define SCAN_MS 250
#define IDLE_MS 60000

struct bar {
    uint16_t x, y, w, h;
    uint8_t rgb[4];
    uint8_t pad[4];
};

static struct fb_var_screeninfo var;
static struct fb_fix_screeninfo fix;
static int fbfd = -1;
static uint8_t *fb;

static unsigned width, height, nframes;
static const struct bar *bars;
static const uint8_t *pixels;

static const char *wanted[] = { "mtk-kpd", "icx_key" };
#define NWANTED (sizeof(wanted) / sizeof(wanted[0]))
static struct pollfd keys[NWANTED];

static long now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* Frame pixels and bar colours come in different byte orders. */
static uint32_t pack(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint32_t) (r >> (8 - var.red.length)) << var.red.offset |
           (uint32_t) (g >> (8 - var.green.length)) << var.green.offset |
           (uint32_t) (b >> (8 - var.blue.length)) << var.blue.offset |
           (var.transp.length ? (uint32_t) ((1u << var.transp.length) - 1) << var.transp.offset : 0);
}

static void put(unsigned x, unsigned y, uint32_t px)
{
    uint8_t *p = fb + y * fix.line_length + x * (var.bits_per_pixel / 8);

    if (var.bits_per_pixel == 32)
        memcpy(p, &px, 4);
    else {
        uint16_t v = (uint16_t) px;
        memcpy(p, &v, 2);
    }
}

/* The frames are stored as B G R A, which is this panel's own layout, so rows
 * are copied as they are; any other layout is converted pixel by pixel. */
static int native(void)
{
    return var.bits_per_pixel == 32 && var.red.offset == 16 && var.green.offset == 8 &&
           var.blue.offset == 0 && var.red.length == 8 && var.green.length == 8 &&
           var.blue.length == 8;
}

static void copy_rect(unsigned frame, unsigned x0, unsigned y0, unsigned w, unsigned h)
{
    const uint8_t *src = pixels + (size_t) frame * width * height * 4;
    const uint8_t *p;
    unsigned x, y;

    for (y = y0; y < y0 + h; y++) {
        p = src + ((size_t) y * width + x0) * 4;
        if (native()) {
            memcpy(fb + y * fix.line_length + x0 * 4, p, (size_t) w * 4);
            continue;
        }
        for (x = x0; x < x0 + w; x++, p += 4)
            put(x, y, pack(p[2], p[1], p[0]));
    }
}

static void flip(void)
{
    var.xoffset = 0;
    var.yoffset = 0;
    var.activate |= FB_ACTIVATE_FORCE;
    ioctl(fbfd, FBIOPUT_VSCREENINFO, &var);
}

/* A frame's countdown area holds its label and, along the bottom, its line,
 * rgb[3] pixels high. left is the countdown still to run, 0..1000, drawn as
 * the filled part of the line; NO_COUNTDOWN clears the whole area to the
 * frame's background, once a key has stopped it. full redraws the frame
 * first. */
#define NO_COUNTDOWN -1

static void draw(unsigned frame, int full, int left)
{
    const struct bar *b = &bars[frame];
    const uint8_t *bg = pixels + ((size_t) frame * width * height + (size_t) b->y * width + b->x) * 4;
    unsigned x, y, fill, line = b->rgb[3];
    uint32_t px;

    if (full)
        copy_rect(frame, 0, 0, width, height);
    if (b->w && left == NO_COUNTDOWN) {
        px = pack(bg[2], bg[1], bg[0]);
        for (y = b->y; y < b->y + b->h; y++)
            for (x = b->x; x < b->x + b->w; x++)
                put(x, y, px);
    } else if (b->w && line) {
        if (!full)
            copy_rect(frame, b->x, b->y + b->h - line, b->w, line);
        fill = b->w * (unsigned) left / 1000;
        px = pack(b->rgb[0], b->rgb[1], b->rgb[2]);
        for (y = b->y + b->h - line; y < b->y + b->h; y++)
            for (x = b->x; x < b->x + fill; x++)
                put(x, y, px);
    }
    flip();
}

static int open_panel(void)
{
    fbfd = open(FB_PATH, O_RDWR | O_CLOEXEC);
    if (fbfd < 0 || ioctl(fbfd, FBIOGET_VSCREENINFO, &var) < 0) {
        perror(FB_PATH);
        return -1;
    }
    var.yoffset = 0;
    if (ioctl(fbfd, FBIOPUT_VSCREENINFO, &var) < 0 || ioctl(fbfd, FBIOGET_FSCREENINFO, &fix) < 0) {
        perror("fb screeninfo");
        return -1;
    }
    if (var.bits_per_pixel != 32 && var.bits_per_pixel != 16) {
        fprintf(stderr, "bootpick: %u bpp is not handled\n", var.bits_per_pixel);
        return -1;
    }
    fb = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (fb == MAP_FAILED) {
        perror("fb mmap");
        return -1;
    }
    return 0;
}

static int load_frames(const char *path)
{
    struct stat st;
    const uint8_t *map;
    size_t need;
    int fd = open(path, O_RDONLY | O_CLOEXEC);

    if (fd < 0 || fstat(fd, &st) < 0) {
        perror(path);
        return -1;
    }
    map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED || st.st_size < 12 || memcmp(map, "NWPK", 4)) {
        fprintf(stderr, "bootpick: %s is not a frames file\n", path);
        return -1;
    }
    width = map[4] | map[5] << 8;
    height = map[6] | map[7] << 8;
    nframes = map[8] | map[9] << 8;
    need = 12 + (size_t) nframes * sizeof(struct bar) + (size_t) nframes * width * height * 4;
    if (nframes < 4 || (size_t) st.st_size != need) {
        fprintf(stderr, "bootpick: %s: %u frames, %lld bytes, want %zu\n",
                path, nframes, (long long) st.st_size, need);
        return -1;
    }
    if (width != var.xres || height != var.yres) {
        fprintf(stderr, "bootpick: frames are %ux%u, the panel is %ux%u\n",
                width, height, var.xres, var.yres);
        return -1;
    }
    bars = (const struct bar *) (map + 12);
    pixels = map + 12 + nframes * sizeof(struct bar);
    return 0;
}

/* Returns how many of the wanted devices are open. Names come from sysfs:
 * opening the touch and sensor nodes just to ask cost about half a second. */
static unsigned scan_keys(int verbose)
{
    char path[48], name[64];
    unsigned i, n, open_count = 0;
    ssize_t len;
    int fd;

    for (n = 0; n < 16; n++) {
        snprintf(path, sizeof(path), "/sys/class/input/event%u/device/name", n);
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            continue;
        len = read(fd, name, sizeof(name) - 1);
        close(fd);
        if (len <= 0)
            continue;
        name[len] = 0;
        name[strcspn(name, "\n")] = 0;
        if (verbose)
            printf("event%u: %s\n", n, name);
        for (i = 0; i < NWANTED; i++) {
            if (keys[i].fd >= 0 || strcmp(name, wanted[i]))
                continue;
            snprintf(path, sizeof(path), "/dev/input/event%u", n);
            keys[i].fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            keys[i].events = POLLIN;
        }
    }
    for (i = 0; i < NWANTED; i++)
        open_count += keys[i].fd >= 0;
    return open_count;
}

/* -1 nothing yet, 0/1 move to that entry, 2 select. */
static int read_key(int fd)
{
    struct input_event ev;
    int action = -1;

    while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type != EV_KEY || ev.value != 1)
            continue;
        switch (ev.code) {
        case KEY_VOLUMEUP:
        case KEY_LEFT:
        case KEY_UP:
        case KEY_PREVIOUSSONG:
            action = 0;
            break;
        case KEY_VOLUMEDOWN:
        case KEY_RIGHT:
        case KEY_DOWN:
        case KEY_NEXTSONG:
            action = 1;
            break;
        case KEY_POWER:
        case KEY_ENTER:
        case KEY_PLAYPAUSE:
            return 2;
        }
    }
    return action;
}

int main(int argc, char **argv)
{
    unsigned i, found;
    int sel, choice = -1, counting, seconds;
    long start, deadline, next_scan, idle_until;

    for (i = 0; i < NWANTED; i++)
        keys[i].fd = -1;

    if (argc == 2 && !strcmp(argv[1], "-i")) {
        if (open_panel() == 0)
            printf("fb0: %ux%u virtual %ux%u, %u bpp, line %u, r%u/%u g%u/%u b%u/%u a%u/%u\n",
                   var.xres, var.yres, var.xres_virtual, var.yres_virtual, var.bits_per_pixel,
                   fix.line_length, var.red.offset, var.red.length, var.green.offset,
                   var.green.length, var.blue.offset, var.blue.length, var.transp.offset,
                   var.transp.length);
        printf("keys: %u of %u\n", scan_keys(1), (unsigned) NWANTED);
        return 0;
    }
    if (argc != 4) {
        fprintf(stderr, "usage: bootpick FRAMES DEFAULT SECONDS | bootpick -i\n");
        return 64;
    }
    sel = atoi(argv[2]) == 2 ? 1 : 0;
    seconds = atoi(argv[3]);
    if (open_panel() < 0 || load_frames(argv[1]) < 0)
        return 70;

    found = scan_keys(0);
    counting = seconds > 0;
    start = now_ms();
    deadline = start + seconds * 1000L;
    next_scan = start + SCAN_MS;
    idle_until = start + IDLE_MS;
    draw(sel, 1, counting ? 1000 : NO_COUNTDOWN);

    while (choice < 0) {
        int timeout = -1;
        long now;

        if (counting)
            timeout = TICK_MS;
        else if (found < NWANTED)
            timeout = SCAN_MS;
        else
            timeout = (int) (idle_until > now_ms() ? idle_until - now_ms() : 0);
        if (poll(keys, NWANTED, timeout) < 0 && errno != EINTR)
            break;
        for (i = 0; i < NWANTED; i++) {
            int action;

            if (keys[i].fd < 0 || !(keys[i].revents & POLLIN))
                continue;
            action = read_key(keys[i].fd);
            if (action < 0)
                continue;
            idle_until = now_ms() + IDLE_MS;
            if (action == 2) {
                choice = sel;
            } else if (action != sel || counting) {
                draw(action, action != sel, NO_COUNTDOWN);
                sel = action;
            }
            counting = 0;
        }
        now = now_ms();
        if (found < NWANTED && now >= next_scan) {
            found = scan_keys(0);
            next_scan = now + SCAN_MS;
        }
        if (choice < 0 && !counting && now >= idle_until)
            choice = sel;
        if (choice < 0 && counting) {
            if (now >= deadline)
                choice = sel;
            else
                draw(sel, 0, (int) ((deadline - now) * 1000 / (seconds * 1000L)));
        }
    }
    if (choice < 0)
        return 71;
    draw(2 + (unsigned) choice, 1, NO_COUNTDOWN);
    return choice + 1;
}
