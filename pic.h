#ifndef PIC_H
#define PIC_H

#include <stdint.h>

void pic_init(void);
void pic_send_eoi(uint8_t irq);
void pic_unmask(uint8_t irq);
void pic_mask(uint8_t irq);

#endif
