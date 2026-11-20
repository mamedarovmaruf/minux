#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>
#include "paging.h"
#include "syscall.h"

#define MAX_TASKS   16
#define KSTACK_SIZE 8192

struct regs;

struct task {
    int          pid;
    int          state;             /* enum task_state */
    char         name[TASK_NAME_MAX];
    uint32_t     esp;               /* saved kernel esp, points at a struct regs */
    uint8_t     *kstack;            /* kernel stack (kmalloc), 0 for idle */
    addrspace_t  as;
    uint32_t     wake_tick;
};

void sched_init(void);              /* the boot context becomes the idle task */

/* All of these return the kernel stack to resume on. */
uint32_t sched_tick(struct regs *r);                    /* timer IRQ */
uint32_t sched_yield(struct regs *r);
uint32_t sched_sleep(struct regs *r, uint32_t ticks);
uint32_t sched_wait_key(struct regs *r);                /* retries the syscall on wake-up */
uint32_t sched_exit(struct regs *r);

void sched_wake_key(void);
int  sched_new_task(const char *name, const addrspace_t *as, uint32_t entry, uint32_t user_esp);
int  sched_kill(int pid);
int  sched_alive(int pid);
int  sched_ps(struct psinfo *out, int max);
const struct task *sched_current(void);
addrspace_t *sched_space(void);

#endif
