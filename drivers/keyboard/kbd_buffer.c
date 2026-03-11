#include <keyboard.h>

#define KBD_BUFFER_SIZE 256

typedef struct {
    char data[KBD_BUFFER_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} kbd_buffer_t;

static kbd_buffer_t kbd_ring;

void kbd_buffer_init(void)
{
    kbd_ring.head = 0;
    kbd_ring.tail = 0;
}

bool kbd_buffer_push(char c)
{
    uint16_t next = (kbd_ring.head + 1) % KBD_BUFFER_SIZE;
    if (next == kbd_ring.tail)
        return false;
    kbd_ring.data[kbd_ring.head] = c;
    kbd_ring.head = next;
    return true;
}

bool kbd_buffer_pop(char *c)
{
    if (kbd_ring.tail == kbd_ring.head)
        return false;
    *c = kbd_ring.data[kbd_ring.tail];
    kbd_ring.tail = (kbd_ring.tail + 1) % KBD_BUFFER_SIZE;
    return true;
}

bool kbd_buffer_empty(void)
{
    return kbd_ring.tail == kbd_ring.head;
}
