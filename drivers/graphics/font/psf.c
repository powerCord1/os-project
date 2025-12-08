#include <stddef.h>
#include <stdint.h>

#include <debug.h>
#include <font.h>
#include <resource.h>

#define PSF_MAGIC 0x864ab572

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t headersize;
    uint32_t flags;
    uint32_t numglyph;
    uint32_t bytesperglyph;
    uint32_t height;
    uint32_t width;
} psf_t;

static psf_t *font;
static void *font_glyphs;

void font_init()
{
    font = (psf_t *)default_font_psfu;
    if (font->magic != PSF_MAGIC) {
        log_err("PSF: Invalid font magic; should be %x, is %x", PSF_MAGIC,
                font->magic);
        return;
    }

    char_width = font->width;
    char_height = font->height;
    font_glyphs = (void *)default_font_psfu + font->headersize;
    log_info("PSF: Font initialized successfully");
    log_verbose("PSF: Version: %d", font->version);
    log_verbose("PSF: Glyphs: %d", font->numglyph);
    log_verbose("PSF: Width: %d", char_width);
    log_verbose("PSF: Height: %d", char_height);
}

void *get_font_glyph(char c)
{
    unsigned char uc = c;
    if (uc >= font->numglyph) {
        // Add diagnostic logging
        // log_warn("get_font_glyph failed for char '%c' (%d)", c, uc);
        // log_warn("font pointer: %p", font);
        if (font) {
            // log_warn("font->numglyph: %d", font->numglyph);
        }
        return NULL;
    }
    return font_glyphs + (uc * font->bytesperglyph);
}