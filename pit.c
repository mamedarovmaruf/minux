#include "pit.h"
#include "irq.h"
#include "pic.h"
#include "io.h"

#define PIT_CH0   0x40
#define PIT_CMD   0x43
#define PIT_BASE  1193182u              /* PIT input clock, Hz */

static volatile uint32_t ticks;
static uint32_t freq;

static void pit_irq(struct regs *r) {
    (void)r;
    ticks++;
}

void pit_init(uint32_t hz) {
    if (hz < 19) hz = 19;               /* divisor must fit in 16 bits */
    if (hz > 1000) hz = 1000;
    freq = hz;

    uint32_t div = PIT_BASE / hz;
    outb(PIT_CMD, 0x36);                /* channel 0, lo/hi byte, mode 3 */
    outb(PIT_CH0, (uint8_t)(div & 0xFF));
    outb(PIT_CH0, (uint8_t)((div >> 8) & 0xFF));

    irq_install_handler(0, pit_irq);
    pic_unmask(0);
}

uint32_t pit_ticks(void) {
    return ticks;
}

uint32_t pit_hz(void) {
    return freq;
}

uint32_t pit_uptime_ms(void) {
    /* No 64-bit math: libgcc is not linked. */
    uint32_t t = ticks;
    return (t / freq) * 1000 + (t % freq) * 1000 / freq;
}

void pit_sleep_ms(uint32_t ms) {
    uint32_t start = pit_uptime_ms();
    while (pit_uptime_ms() - start < ms) {
        __asm__ volatile ("hlt");
    }
}
