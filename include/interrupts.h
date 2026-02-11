#include <stdbool.h>
#include <stdint.h>

#include <isr.h>

void idt_init();
void enable_interrupts();
void disable_interrupts();
bool are_interrupts_enabled();
void exception_install_handler(uint8_t vector,
                               void (*handler)(interrupt_frame_t *));
void exception_uninstall_handler(uint8_t vector);
uint64_t exception_dispatch(uint64_t rsp, uint8_t vector);
void irq_install_handler(uint8_t irq, uint64_t (*handler)(uint64_t, void *),
                         void *ctx);
void irq_uninstall_handler(uint8_t irq, uint64_t (*handler)(uint64_t, void *),
                           void *ctx);
uint64_t irq_dispatch(uint64_t rsp, uint8_t irq);
void register_exceptions();

static struct irq_handler_entry *irq_handlers[16];
static void (*exception_handlers[32])(interrupt_frame_t *);

struct irq_handler_entry {
    uint64_t (*handler)(uint64_t, void *);
    void *ctx;
    struct irq_handler_entry *next;
};

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