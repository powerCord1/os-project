#pragma once

#include <stdint.h>

extern uint32_t char_width;
extern uint32_t char_height;

void font_init(void);

// Get the pixel map of a glyph
void *get_font_glyph(char c);

// Return the pixel width of a string
uint32_t get_string_width(char *str);
