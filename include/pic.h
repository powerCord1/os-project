#include <stdint.h>

void pic_sendEOI(uint8_t irq);
void pic_init(void);
void pic_disable(void);
void irq_set_mask(uint8_t IRQline);
void irq_clear_mask(uint8_t IRQline);
static uint16_t __pic_get_irq_reg(int ocw3);
uint16_t pic_get_irr(void);
uint16_t pic_get_isr(void);