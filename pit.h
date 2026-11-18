#ifndef PIT_H
#define PIT_H

#include <stdint.h>

void     pit_init(uint32_t hz);
uint32_t pit_ticks(void);               /* ticks since pit_init */
uint32_t pit_hz(void);
uint32_t pit_uptime_ms(void);
void     pit_sleep_ms(uint32_t ms);     /* needs interrupts enabled */

#endif
