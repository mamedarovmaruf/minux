#include <stdint.h>
#include "terminal.h"
#include "ulib.h"

int main(void) {
    for (;;) {
        uint32_t s = sys_uptime_ms() / 1000;
        char t[8] = {
            (char)('0' + s / 36000 % 10), (char)('0' + s / 3600 % 10), ':',
            (char)('0' + s / 600 % 6),    (char)('0' + s / 60 % 10),   ':',
            (char)('0' + s / 10 % 6),     (char)('0' + s % 10)
        };
        uint32_t col = term_cols() - 9;
        for (uint32_t i = 0; i < 8; i++) {
            put_char_at(0, col + i, t[i], VGA_YELLOW | (VGA_BLUE << 4));
        }
        sys_sleep(250);
    }
}
