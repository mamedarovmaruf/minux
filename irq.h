#ifndef IRQ_H
#define IRQ_H

#include "panic.h"

typedef void (*irq_handler_t)(struct regs *r);

void irq_install_handler(int irq, irq_handler_t handler);
uint32_t irq_handler(struct regs *r);   /* returns the stack to resume on */

#endif
