#include <stdint.h>
#include "mos.h"
#include "terminal.h"
#include "keyboard.h"
#include "kstring.h"
#include "io.h"

#define MOS_VERSION "0.1"
#define LINE_MAX 128
#define ARGS_MAX 16

typedef void (*mos_cmd_fn)(int argc, char **argv);

struct mos_cmd {
    const char *name;
    const char *help;
    mos_cmd_fn fn;
};

static void cmd_help(int argc, char **argv);
static void cmd_clear(int argc, char **argv);
static void cmd_echo(int argc, char **argv);
static void cmd_mos(int argc, char **argv);
static void cmd_mem(int argc, char **argv);
static void cmd_reboot(int argc, char **argv);
static void cmd_halt(int argc, char **argv);

static const struct mos_cmd commands[] = {
    { "help",   "show this list",             cmd_help   },
    { "clear",  "clear the screen",           cmd_clear  },
    { "echo",   "print arguments",            cmd_echo   },
    { "mos",    "show shell information",     cmd_mos    },
    { "mem",    "show memory information",    cmd_mem    },
    { "reboot", "restart the computer",       cmd_reboot },
    { "halt",   "stop the system",            cmd_halt   },
};

#define CMD_COUNT ((int)(sizeof(commands) / sizeof(commands[0])))

static void prompt(void) {
    set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    print_string("mos");
    set_color(VGA_WHITE, VGA_BLACK);
    print_string("> ");
    set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void banner(void) {
    set_color(VGA_WHITE, VGA_BLACK);
    print_string("MOS ");
    print_string(MOS_VERSION);
    print_string(" - Minux Shell\n");
    set_color(VGA_LIGHT_GREY, VGA_BLACK);
    print_string("Type 'help' for a list of commands.\n\n");
}

static void cmd_help(int argc, char **argv) {
    (void)argc;
    (void)argv;
    for (int i = 0; i < CMD_COUNT; i++) {
        print_string("  ");
        print_string(commands[i].name);
        int pad = 8 - (int)kstrlen(commands[i].name);
        while (pad-- > 0) {
            print_char(' ');
        }
        print_string(commands[i].help);
        print_char('\n');
    }
}

static void cmd_clear(int argc, char **argv) {
    (void)argc;
    (void)argv;
    clear_screen();
}

static void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) {
            print_char(' ');
        }
        print_string(argv[i]);
    }
    print_char('\n');
}

static void cmd_mos(int argc, char **argv) {
    (void)argc;
    (void)argv;
    banner();
}

static void cmd_mem(int argc, char **argv) {
    (void)argc;
    (void)argv;
    print_string("Video memory: ");
    print_hex(0xB8000);
    print_char('\n');
    print_string("Kernel entry: ");
    print_hex((uint32_t)(uintptr_t)&mos_run);
    print_char('\n');
}

static void cmd_reboot(int argc, char **argv) {
    (void)argc;
    (void)argv;
    print_string("Rebooting...\n");
    __asm__ volatile ("cli");
    for (int i = 0; i < 1000; i++) {
        if (!(inb(0x64) & 2)) {
            break;
        }
    }
    outb(0x64, 0xFE);
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static void cmd_halt(int argc, char **argv) {
    (void)argc;
    (void)argv;
    print_string("System halted.\n");
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static int tokenize(char *line, char **argv) {
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') {
            *p++ = 0;
        }
        if (!*p) {
            break;
        }
        if (argc < ARGS_MAX) {
            argv[argc++] = p;
        }
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }
    }
    return argc;
}

static void execute(char *line) {
    char *argv[ARGS_MAX];
    int argc = tokenize(line, argv);
    if (argc == 0) {
        return;
    }
    for (int i = 0; i < CMD_COUNT; i++) {
        if (kstrcmp(argv[0], commands[i].name) == 0) {
            commands[i].fn(argc, argv);
            return;
        }
    }
    set_color(VGA_LIGHT_RED, VGA_BLACK);
    print_string("mos: unknown command: ");
    print_string(argv[0]);
    print_char('\n');
    set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

void mos_run(void) {
    char line[LINE_MAX];
    uint32_t len = 0;

    banner();
    prompt();

    for (;;) {
        char c = keyboard_getchar();

        if (c == '\n') {
            print_char('\n');
            line[len] = 0;
            execute(line);
            len = 0;
            prompt();
        } else if (c == '\b') {
            if (len > 0) {
                len--;
                print_char('\b');
            }
        } else if (c == 3) {
            print_string("^C\n");
            len = 0;
            prompt();
        } else if (c >= 32 && c < 127) {
            if (len < LINE_MAX - 1) {
                line[len++] = c;
                print_char(c);
            }
        }
    }
}
