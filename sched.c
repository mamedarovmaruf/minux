#include "sched.h"
#include "gdt.h"
#include "kheap.h"
#include "kstring.h"
#include "panic.h"
#include "pit.h"

/* Round-robin scheduler. Task 0 is the idle task (the boot context).
   The kernel is not preemptible: interrupt gates clear IF, and the timer
   only switches away from user code or from idle. A switch is just
   "return a different stack from the interrupt handler". */

static struct task tasks[MAX_TASKS];
static struct task *current;
static int next_pid = 1;

static void copy_name(char *dst, const char *src) {
    int i = 0;
    while (src[i] && i < TASK_NAME_MAX - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

void sched_init(void) {
    kmemset(tasks, 0, sizeof(tasks));
    tasks[0].pid   = 0;
    tasks[0].state = TASK_READY;
    tasks[0].as    = *paging_kernel_space();
    copy_name(tasks[0].name, "idle");
    current = &tasks[0];
}

/* Free tasks that exited. Never the running one: we are on its stack. */
static void reap(void) {
    for (int i = 1; i < MAX_TASKS; i++) {
        struct task *t = &tasks[i];
        if (t->state != TASK_ZOMBIE || t == current) {
            continue;
        }
        paging_space_destroy(&t->as);
        kfree(t->kstack);
        t->kstack = 0;
        t->state = TASK_FREE;
    }
}

static struct task *pick(void) {
    int cur = (int)(current - tasks);
    for (int i = 1; i <= MAX_TASKS; i++) {
        struct task *t = &tasks[(cur + i) % MAX_TASKS];
        if (t != &tasks[0] && t->state == TASK_READY) {
            return t;
        }
    }
    return &tasks[0];
}

static uint32_t reschedule(struct regs *r) {
    struct task *prev = current;
    prev->esp = (uint32_t)(uintptr_t)r;

    reap();
    struct task *next = pick();
    if (next != prev) {
        current = next;
        if (next->kstack) {
            tss_set_stack((uint32_t)(uintptr_t)next->kstack + KSTACK_SIZE);
        }
        paging_switch(&next->as);
    }
    return next->esp;
}

uint32_t sched_tick(struct regs *r) {
    if (!current) {                 /* timer before sched_init() */
        return (uint32_t)(uintptr_t)r;
    }
    uint32_t now = pit_ticks();
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_SLEEP && (int32_t)(now - tasks[i].wake_tick) >= 0) {
            tasks[i].state = TASK_READY;
        }
    }
    /* Kernel code (a syscall in progress) is never preempted. */
    if (!(r->cs & 3) && current != &tasks[0]) {
        return (uint32_t)(uintptr_t)r;
    }
    return reschedule(r);
}

uint32_t sched_yield(struct regs *r) {
    return reschedule(r);
}

uint32_t sched_sleep(struct regs *r, uint32_t ticks) {
    current->wake_tick = pit_ticks() + ticks;
    current->state = TASK_SLEEP;
    return reschedule(r);
}

uint32_t sched_wait_key(struct regs *r) {
    r->eip -= 2;                    /* re-run the 2-byte int 0x80 when woken */
    current->state = TASK_WAIT_KEY;
    return reschedule(r);
}

uint32_t sched_exit(struct regs *r) {
    current->state = TASK_ZOMBIE;
    return reschedule(r);
}

void sched_wake_key(void) {
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_WAIT_KEY) {
            tasks[i].state = TASK_READY;
        }
    }
}

/* Build a task that first runs in ring 3: fake the frame an interrupt
   would have left on its kernel stack, the isr.asm exit path does the rest. */
int sched_new_task(const char *name, const addrspace_t *as, uint32_t entry, uint32_t user_esp) {
    struct task *t = 0;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_FREE) {
            t = &tasks[i];
            break;
        }
    }
    if (!t) return -1;

    uint8_t *stack = kmalloc(KSTACK_SIZE);
    if (!stack) return -1;

    struct regs *r = (struct regs *)(uintptr_t)((uint32_t)(uintptr_t)stack + KSTACK_SIZE - sizeof(struct regs));
    kmemset(r, 0, sizeof(*r));
    r->gs = r->fs = r->es = r->ds = USER_DS;
    r->eip     = entry;
    r->cs      = USER_CS;
    r->eflags  = 0x202;             /* IF set, IOPL 0 */
    r->useresp = user_esp;
    r->ss      = USER_DS;

    t->pid       = next_pid++;
    t->esp       = (uint32_t)(uintptr_t)r;
    t->kstack    = stack;
    t->as        = *as;
    t->wake_tick = 0;
    copy_name(t->name, name);
    t->state     = TASK_READY;      /* last: the timer may run any time */
    return t->pid;
}

int sched_kill(int pid) {
    if (pid <= 0) return -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        struct task *t = &tasks[i];
        if (t->pid == pid && t->state != TASK_FREE && t->state != TASK_ZOMBIE) {
            if (t == current) return -2;    /* use exit for that */
            t->state = TASK_ZOMBIE;
            return 0;
        }
    }
    return -1;
}

int sched_alive(int pid) {
    if (pid <= 0) return 0;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].pid == pid && tasks[i].state != TASK_FREE && tasks[i].state != TASK_ZOMBIE) {
            return 1;
        }
    }
    return 0;
}

int sched_ps(struct psinfo *out, int max) {
    int n = 0;
    for (int i = 0; i < MAX_TASKS && n < max; i++) {
        struct task *t = &tasks[i];
        if (t->state == TASK_FREE) continue;
        out[n].pid   = (uint32_t)t->pid;
        out[n].state = (t == current) ? TASK_RUNNING : (uint32_t)t->state;
        copy_name(out[n].name, t->name);
        n++;
    }
    return n;
}

const struct task *sched_current(void) {
    return current;
}

addrspace_t *sched_space(void) {
    return &current->as;
}
