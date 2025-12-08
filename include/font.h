#pragma once

#include <stdint.h>

extern uint32_t char_width;
extern uint32_t char_height;

void font_init(void);
void *get_font_glyph(char c);
uint32_t get_string_width(char *str);
