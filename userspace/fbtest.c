#include "api.h"

void _start(void)
{
    unsigned int info[4];
    fb_info(info);
    unsigned int w = info[0];
    unsigned int h = info[1];

    unsigned int buf = fb_alloc(w, h);
    unsigned int *pixels = (unsigned int *)buf;

    for (unsigned int y = 0; y < h; y++)
        for (unsigned int x = 0; x < w; x++)
            pixels[y * w + x] = ((x * 255 / w) << 16) | ((y * 255 / h) << 8);

    fb_flush(buf, 0, 0, w, h, w * 4);
    getchar();
}
