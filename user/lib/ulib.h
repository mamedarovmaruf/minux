#ifndef ULIB_H
#define ULIB_H

#include <stdint.h>
#include "syscall.h"

void     sys_exit(int code) __attribute__((noreturn));
void     sys_sleep(uint32_t ms);
void     sys_yield(void);
uint32_t sys_uptime_ms(void);
int      sys_spawn(const char *name);
int      sys_kill(int pid);
int      sys_ps(struct psinfo *out, int max);
void     sys_meminfo(struct meminfo *out);
void     sys_reboot(void) __attribute__((noreturn));
void     sys_halt(void) __attribute__((noreturn));

#endif
