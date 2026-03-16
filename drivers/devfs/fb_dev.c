#include <devfs.h>
#include <framebuffer.h>
#include <limine_defs.h>
#include <string.h>

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

typedef struct {
    uint32_t xres, yres;
    uint32_t xres_virtual, yres_virtual;
    uint32_t xoffset, yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    struct { uint32_t offset, length, msb_right; } red, green, blue, transp;
} fb_var_screeninfo_t;

typedef struct {
    char id[16];
    uint32_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t type_aux;
    uint32_t visual;
    uint16_t xpanstep, ypanstep, ywrapstep;
    uint32_t line_length;
    uint32_t mmio_start;
    uint32_t mmio_len;
    uint32_t accel;
    uint16_t capabilities;
    uint16_t reserved[2];
} fb_fix_screeninfo_t;

static int fb_open(void **state)
{
    *state = NULL;
    return 0;
}

static int64_t fb_read(void *state, uint8_t *buf, uint32_t count,
                       uint64_t offset, bool nonblock)
{
    (void)state;
    (void)nonblock;
    uint32_t fb_size = fb->height * fb->pitch;
    if (offset >= fb_size)
        return 0;
    uint32_t avail = fb_size - (uint32_t)offset;
    if (count > avail)
        count = avail;
    memcpy(buf, (const uint8_t *)fb_ptr + offset, count);
    return count;
}

static int64_t fb_write(void *state, const uint8_t *buf, uint32_t count,
                        uint64_t offset)
{
    (void)state;
    uint32_t fb_size = fb->height * fb->pitch;
    if (offset >= fb_size)
        return 0;
    uint32_t avail = fb_size - (uint32_t)offset;
    if (count > avail)
        count = avail;
    memcpy((uint8_t *)fb_ptr + offset, buf, count);
    return count;
}

static int64_t fb_ioctl(void *state, int32_t req, void *argp, uint32_t size)
{
    (void)state;

    if (req == FBIOGET_VSCREENINFO) {
        if (size < sizeof(fb_var_screeninfo_t))
            return -22; /* EINVAL */
        fb_var_screeninfo_t *v = (fb_var_screeninfo_t *)argp;
        memset(v, 0, sizeof(*v));
        v->xres = fb->width;
        v->yres = fb->height;
        v->xres_virtual = fb->width;
        v->yres_virtual = fb->height;
        v->bits_per_pixel = fb->bpp;
        v->red.offset = fb->red_mask_shift;
        v->red.length = fb->red_mask_size;
        v->green.offset = fb->green_mask_shift;
        v->green.length = fb->green_mask_size;
        v->blue.offset = fb->blue_mask_shift;
        v->blue.length = fb->blue_mask_size;
        return 0;
    }

    if (req == FBIOGET_FSCREENINFO) {
        if (size < sizeof(fb_fix_screeninfo_t))
            return -22;
        fb_fix_screeninfo_t *f = (fb_fix_screeninfo_t *)argp;
        memset(f, 0, sizeof(*f));
        strncpy(f->id, "limine_fb", sizeof(f->id));
        f->line_length = fb->pitch;
        f->smem_len = fb->height * fb->pitch;
        return 0;
    }

    return -25; /* ENOTTY */
}

static bool fb_poll_readable(void *state)
{
    (void)state;
    return false;
}

static bool fb_poll_writable(void *state)
{
    (void)state;
    return true;
}

static void fb_close(void *state)
{
    (void)state;
}

void fb_dev_init(void)
{
    static const devfs_device_t fb_dev = {
        .name = "fb0",
        .open = fb_open,
        .read = fb_read,
        .write = fb_write,
        .ioctl = fb_ioctl,
        .poll_readable = fb_poll_readable,
        .poll_writable = fb_poll_writable,
        .close = fb_close,
    };
    devfs_register(&fb_dev);
}
