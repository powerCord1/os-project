#include <stdbool.h>
#include <stdint.h>

void idt_init();
void enable_interrupts();
void disable_interrupts();
bool are_interrupts_enabled();
void check_interrupts();
void irq_install_handler(uint8_t irq, void (*handler)(void *), void *ctx);

struct irq_handler_entry {
    void (*handler)(void *);
    void *ctx;
};

static struct irq_handler_entry irq_handlers[16];

enum irq_types {
    IRQ_TYPE_PIT = 0,
    IRQ_TYPE_KEYBOARD = 1,
    IRQ_TYPE_CASCADE = 2,
    IRQ_TYPE_COM2 = 3,
    IRQ_TYPE_COM1 = 4,
    IRQ_TYPE_LPT2 = 5,
    IRQ_TYPE_FLOPPY_DISK = 6,
    IRQ_TYPE_LPT1 = 7,
    IRQ_TYPE_CMOS = 8,
    IRQ_TYPE_MOUSE = 12,
    IRQ_TYPE_COPROCESSOR = 13,
    IRQ_TYPE_HDD_1 = 14,
    IRQ_TYPE_HDD_2 = 15
};