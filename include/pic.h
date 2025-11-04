#include <stdint.h>

void pic_init(void);
void pic_sendEOI(uint8_t irq);
void pic_sendEOI_master();
void pic_disable(void);
void irq_set_mask(uint8_t IRQline);
void irq_clear_mask(uint8_t IRQline);