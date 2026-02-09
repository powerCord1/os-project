#include <stdint.h>

// Initalise the PIC
void pic_init();

// Send 'end of interrupt' signal to the slave PIC
void pic_sendEOI(uint8_t irq);

// Send 'end of interrupt' signal to the master PIC
void pic_sendEOI_master();

// Disable the PIC
void pic_disable();

// Set an IRQ mask
void irq_set_mask(uint8_t IRQline);

// Clear an IRQ mask
void irq_clear_mask(uint8_t IRQline);

void irq_mask_all();

void irq_unmask_all();