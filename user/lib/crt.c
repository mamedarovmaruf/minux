#include "ulib.h"

extern int main(void);

__attribute__((section(".text._start")))
void _start(void) {
    sys_exit(main());
}
