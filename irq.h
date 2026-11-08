#ifndef IRQ_H
#define IRQ_H

#include "panic.h"

typedef void (*irq_handler_t)(struct regs *r);

void irq_install_handler(int irq, irq_handler_t handler);
void irq_handler(struct regs *r);

#endif
