#include "api.h"

void _start(void)
{
    int lines = 0, words = 0, chars = 0;
    int in_word = 0;

    while (1) {
        char c;
        int n = read(0, &c, 1);
        if (n <= 0)
            break;
        chars++;
        if (c == '\n')
            lines++;
        if (c == ' ' || c == '\n' || c == '\t') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }

    print_num(lines);
    puts(" ");
    print_num(words);
    puts(" ");
    print_num(chars);
    puts("\n");
    exit(0);
}
