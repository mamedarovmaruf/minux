#include <stdint.h>
#include "mos.h"
#include "terminal.h"
#include "keyboard.h"
#include "kstring.h"
#include "fat.h"
#include "io.h"

#define MOS_VERSION "0.3"
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
static void cmd_ls(int argc, char **argv);
static void cmd_cat(int argc, char **argv);
static void cmd_touch(int argc, char **argv);
static void cmd_write(int argc, char **argv);
static void cmd_append(int argc, char **argv);
static void cmd_rm(int argc, char **argv);
static void cmd_df(int argc, char **argv);
static void cmd_format(int argc, char **argv);
static void cmd_reboot(int argc, char **argv);
static void cmd_halt(int argc, char **argv);

static const struct mos_cmd commands[] = {
    { "help",   "show this list",                  cmd_help   },
    { "clear",  "clear the screen",                cmd_clear  },
    { "echo",   "print arguments",                 cmd_echo   },
    { "mos",    "show shell information",          cmd_mos    },
    { "mem",    "show memory information",         cmd_mem    },
    { "ls",     "list files",                      cmd_ls     },
    { "cat",    "print a file",                    cmd_cat    },
    { "touch",  "create an empty file",            cmd_touch  },
    { "write",  "write text to a file",            cmd_write  },
    { "append", "append a line to a file",         cmd_append },
    { "rm",     "delete a file",                   cmd_rm     },
    { "df",     "show disk usage",                 cmd_df     },
    { "format", "format the disk (format yes)",    cmd_format },
    { "reboot", "restart the computer",            cmd_reboot },
    { "halt",   "stop the system",                 cmd_halt   },
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

static void fail(const char *cmd, const char *msg) {
    set_color(VGA_LIGHT_RED, VGA_BLACK);
    print_string(cmd);
    print_string(": ");
    print_string(msg);
    print_char('\n');
    set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* joins argv[start..] with single spaces and ends the result with '\n' */
static uint32_t join_args(int argc, char **argv, int start, char *buf) {
    uint32_t len = 0;
    for (int i = start; i < argc; i++) {
        if (i > start) {
            buf[len++] = ' ';
        }
        uint32_t n = kstrlen(argv[i]);
        kmemcpy(buf + len, argv[i], n);
        len += n;
    }
    buf[len++] = '\n';
    return len;
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

static void cmd_ls(int argc, char **argv) {
    (void)argc;
    (void)argv;
    uint32_t cursor = 0;
    struct fat_entry e;
    int count = 0;
    int r;

    while ((r = fat_readdir(&cursor, &e)) > 0) {
        print_string("  ");
        print_string(e.name);
        int pad = FAT_NAME_MAX + 2 - (int)kstrlen(e.name);
        while (pad-- > 0) {
            print_char(' ');
        }
        if (e.is_dir) {
            print_string("<DIR>");
        } else {
            print_dec(e.size);
            print_string(" bytes");
        }
        print_char('\n');
        count++;
    }
    if (r < 0) {
        fail("ls", fat_strerror(r));
        return;
    }
    print_dec((uint32_t)count);
    print_string(" file(s)\n");
}

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) {
        fail("cat", "usage: cat <file>");
        return;
    }
    char buf[512];
    char last = '\n';
    uint32_t off = 0;
    int n;

    while ((n = fat_read(argv[1], off, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            print_char(buf[i]);
        }
        last = buf[n - 1];
        off += (uint32_t)n;
    }
    if (n < 0) {
        fail("cat", fat_strerror(n));
        return;
    }
    if (last != '\n') {
        print_char('\n');
    }
}

static void cmd_touch(int argc, char **argv) {
    if (argc < 2) {
        fail("touch", "usage: touch <file>");
        return;
    }
    int r = fat_create(argv[1]);
    if (r != FAT_OK && r != FAT_ERR_EXISTS) {
        fail("touch", fat_strerror(r));
    }
}

static void cmd_write(int argc, char **argv) {
    if (argc < 3) {
        fail("write", "usage: write <file> <text>");
        return;
    }
    char buf[LINE_MAX + 2];
    uint32_t len = join_args(argc, argv, 2, buf);
    int r = fat_write(argv[1], buf, len);
    if (r != FAT_OK) {
        fail("write", fat_strerror(r));
    }
}

static void cmd_append(int argc, char **argv) {
    if (argc < 3) {
        fail("append", "usage: append <file> <text>");
        return;
    }
    char buf[LINE_MAX + 2];
    uint32_t len = join_args(argc, argv, 2, buf);
    int r = fat_append(argv[1], buf, len);
    if (r != FAT_OK) {
        fail("append", fat_strerror(r));
    }
}

static void cmd_rm(int argc, char **argv) {
    if (argc < 2) {
        fail("rm", "usage: rm <file>");
        return;
    }
    int r = fat_remove(argv[1]);
    if (r != FAT_OK) {
        fail("rm", fat_strerror(r));
    }
}

static void cmd_df(int argc, char **argv) {
    (void)argc;
    (void)argv;
    struct fat_info info;
    int r = fat_get_info(&info);
    if (r != FAT_OK) {
        fail("df", fat_strerror(r));
        return;
    }
    uint32_t total_kb = info.total_clusters * (info.cluster_bytes / 1024);
    uint32_t free_kb = info.free_clusters * (info.cluster_bytes / 1024);

    print_string("Disk:  ");
    print_dec(info.disk_sectors / 2);
    print_string(" KB\n");
    print_string("Total: ");
    print_dec(total_kb);
    print_string(" KB\n");
    print_string("Used:  ");
    print_dec(total_kb - free_kb);
    print_string(" KB\n");
    print_string("Free:  ");
    print_dec(free_kb);
    print_string(" KB\n");
}

static void cmd_format(int argc, char **argv) {
    if (argc < 2 || kstrcmp(argv[1], "yes") != 0) {
        fail("format", "this erases all files, run 'format yes' to confirm");
        return;
    }
    int r = fat_format();
    if (r != FAT_OK) {
        fail("format", fat_strerror(r));
        return;
    }
    print_string("Disk formatted as FAT16.\n");
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
