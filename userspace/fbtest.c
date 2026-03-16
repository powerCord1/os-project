#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

struct fb_bitfield {
    uint32_t offset, length, msb_right;
};

struct fb_var_screeninfo {
    uint32_t xres, yres;
    uint32_t xres_virtual, yres_virtual;
    uint32_t xoffset, yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    struct fb_bitfield red, green, blue, transp;
};

struct fb_fix_screeninfo {
    char id[16];
    uint32_t smem_start, smem_len;
    uint32_t type, type_aux, visual;
    uint16_t xpanstep, ypanstep, ywrapstep;
    uint32_t line_length;
    uint32_t mmio_start, mmio_len, accel;
    uint16_t capabilities, reserved[2];
};

int main(void)
{
    int fb = open("/dev/fb0", O_RDWR);
    if (fb < 0)
        return 1;

    struct fb_var_screeninfo vinfo;
    ioctl(fb, FBIOGET_VSCREENINFO, &vinfo);
    uint32_t w = vinfo.xres;
    uint32_t h = vinfo.yres;

    struct fb_fix_screeninfo finfo;
    ioctl(fb, FBIOGET_FSCREENINFO, &finfo);
    uint32_t pitch = finfo.line_length;

    uint32_t row[w];
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++)
            row[x] = ((x * 255 / w) << 16) | ((y * 255 / h) << 8);
        lseek(fb, y * pitch, SEEK_SET);
        write(fb, row, w * 4);
    }

    read(STDIN_FILENO, row, 1);
    close(fb);
    return 0;
}
