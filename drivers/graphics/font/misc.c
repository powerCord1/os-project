#include <font.h>
#include <string.h>

uint32_t char_width;
uint32_t char_height;

uint32_t get_string_width(char *str)
{
    return strlen(str) * char_width;
}