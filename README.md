# minux

An educational monolithic hobby kernel for x86 (32-bit), written in C and NASM.

Minux boots in QEMU or on real hardware via Multiboot, sets up its own interrupt handling, reads the PS/2 keyboard and runs a small command shell called MOS.

## Project structure

```
minux/
├── boot.asm        Entry point: Multiboot header, stack setup, call into C
├── isr.asm         CPU exception stubs (0-31) and IRQ stubs (32-47)
├── linker.ld       Memory layout: kernel is loaded at 0x100000 (1 MB)
├── kernel.c        kernel_main(): hardware init, then hands control to the shell
├── idt.c / idt.h   Interrupt Descriptor Table setup
├── pic.c / pic.h   8259 PIC remapping, EOI, IRQ mask/unmask
├── irq.c / irq.h   IRQ dispatcher and handler registration
├── io.h            inb / outb / io_wait port I/O helpers
├── panic.c / panic.h   Exception handler and kernel panic screen
├── terminal.c / terminal.h   VGA text-mode driver
├── keyboard.c / keyboard.h   PS/2 keyboard driver (scancode set 1)
├── mos.c / mos.h   MOS shell: line editing, parser, built-in commands
├── kstring.c / kstring.h   Minimal string functions (no libc in the kernel)
├── src/main.rs     Build & run tool written in Rust (replacement for a Makefile)
├── Cargo.toml      Cargo manifest for the build tool
└── README.md
```

## How the pieces fit together

1. The bootloader (GRUB or QEMU `-kernel`) finds the Multiboot header in `boot.asm` and jumps to `start`.
2. `start` sets up the stack and calls `kernel_main`.
3. `kernel_main` runs the initialisation sequence:
   - `pic_init()` remaps the two 8259 PICs so that IRQ0-15 land on vectors 32-47 instead of colliding with CPU exceptions.
   - `idt_init()` fills the IDT: vectors 0-31 point to exception stubs, vectors 32-47 point to IRQ stubs.
   - `keyboard_init()` drains the controller buffer, registers the keyboard IRQ handler and unmasks IRQ1.
   - `sti` enables hardware interrupts.
4. `kernel_main` calls `mos_run()`, which reads keys through `keyboard_getchar()` and executes commands.

Interrupt path: a hardware IRQ enters an `irqN` stub in `isr.asm`, which pushes a register frame and calls `irq_handler()`. The dispatcher calls the registered handler (for example `keyboard_irq()`) and sends EOI to the PIC.

CPU exceptions take the same route through `isr_common` and `isr_handler()`, which prints a panic screen with the register dump. Page faults also print CR2.

## Components

### Terminal driver

`terminal.c` writes directly to VGA text memory at `0xB8000`. Each character takes two bytes: the ASCII code and an attribute byte (foreground and background colour). The screen is 80x25 characters.

It provides `clear_screen()`, `set_color(fg, bg)`, `print_char`, `print_string`, `print_hex` and `print_dec`. The control characters `\n`, `\r`, `\t` and `\b` are handled, and the screen scrolls when full. The hardware cursor is moved through the VGA ports `0x3D4` and `0x3D5`.

### Interrupts

`idt.c` builds a 256-entry table of 32-bit interrupt gates using the current code segment selector. Gates 0-31 are CPU exceptions, 32-47 are the remapped hardware IRQs. Other vectors are left empty.

`pic.c` programs both 8259 PICs, sends end-of-interrupt (EOI) and manages the IRQ mask registers. The slave PIC is cascaded on IRQ2. After initialisation only IRQ2 is unmasked on the master, so devices must be unmasked explicitly with `pic_unmask()`.

`irq.c` keeps a table of 16 handlers. `irq_install_handler(irq, fn)` registers one, and `irq_handler()` dispatches the interrupt and sends EOI.

`panic.c` handles CPU exceptions. It switches to a red screen and dumps the exception name, error code, EIP, CS, EFLAGS and general registers. For page faults it also decodes the faulting address and access type. Afterwards the CPU is halted.

### Keyboard

`keyboard.c` reads scancodes from port `0x60` in IRQ1. It supports scancode set 1, which QEMU and most BIOS emulations use by default.

Supported: letters, digits, punctuation, Enter, Backspace, Tab, Space, Shift, Caps Lock and Ctrl (Ctrl+letter produces a control code, so Ctrl+C is `0x03`). Extended `0xE0` keys such as arrows are read and currently ignored.

Key codes go into a 256-byte ring buffer. `keyboard_getchar()` blocks with `sti; hlt` until a character is available. If the buffer is full, new characters are dropped.

### MOS shell

`mos.c` implements a line-based shell. The prompt is `mos> `. Input is collected into a 127-character line buffer. Backspace removes the last character from the line and the screen. Ctrl+C discards the current line.

The line is split on spaces and tabs into at most 16 arguments. The first argument is matched against the command table, and an unknown name prints an error.

Built-in commands:

| Command  | Description                              |
|----------|------------------------------------------|
| `help`   | list all commands                        |
| `clear`  | clear the screen                         |
| `echo`   | print the arguments                      |
| `mos`    | show the shell banner and version        |
| `mem`    | show the video memory and kernel entry addresses |
| `reboot` | restart the machine through the keyboard controller (`0xFE` on port `0x64`) |
| `halt`   | stop the CPU with `cli; hlt`             |

To add a command, write a `cmd_*` function in `mos.c` with the signature `void f(int argc, char **argv)` and add one entry to the `commands[]` array.

### Support library

The kernel is built with `-nostdlib`, so there is no C library. `kstring.c` provides `kstrlen` and `kstrcmp`. Other functions are added there as needed.

## Requirements

Linux or WSL2 is required. The build uses 32-bit ELF output, which is not available on macOS or native Windows without a cross-compiler.

```bash
sudo apt install nasm gcc binutils qemu-system-x86 cargo
```

## Build and run

```bash
cargo run                 # build and launch in QEMU
cargo run -- --no-run     # build only -> build/kernel.elf
cargo run -- --clean      # remove build/ and rebuild everything
cargo run -- --debug      # QEMU waits for GDB on localhost:1234
cargo run -- --help       # show all options
```

The build tool compiles every `.asm` and `.c` file in the project root and links them with `linker.ld`. New source files are picked up automatically. Changing a `.h` file rebuilds all `.c` files. The tool also checks that `nasm`, `gcc` or `clang`, `ld` and `qemu-system-x86_64` are installed and prints the apt command if something is missing. Set `CC=clang` to use clang, and `NO_COLOR=1` to disable coloured output.

### Manual build (without Cargo)

```bash
mkdir -p build
for f in *.asm; do nasm -f elf32 "$f" -o "build/${f%.asm}.asm.o"; done
for f in *.c; do gcc -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -nostdlib -O2 -Wall -Wextra -c "$f" -o "build/${f%.c}.o"; done
ld -m elf_i386 -z noexecstack -T linker.ld -o build/kernel.elf build/*.o
qemu-system-x86_64 -kernel build/kernel.elf
```

## Debugging

Run `cargo run -- --debug` in one terminal, then in another:

```bash
gdb build/kernel.elf -ex 'target remote :1234'
```

## Roadmap

- [x] Interrupt descriptor table (32 CPU exceptions and 16 IRQs)
- [x] Exception handler with panic screen
- [x] PIC remapping and IRQ dispatch
- [x] PS/2 keyboard driver (scancode set 1, Shift, Caps Lock, Ctrl)
- [x] MOS shell with line editing and built-in commands
- [ ] Own GDT
- [ ] Timer (IRQ0) and `uptime` command
- [ ] Command history and arrow keys (decode `0xE0` sequences)
- [ ] Function keys and extended keys
- [ ] Memory management (physical allocator, paging)
- [ ] Process scheduling

## Screenshots

<img width="515" height="498" alt="Снимок экрана от 2026-11-08 16-11-21" src="https://github.com/user-attachments/assets/0d289e5a-2f27-44d1-930c-26062cf8d614" />

<img width="724" height="401" alt="panic" src="https://github.com/user-attachments/assets/d269442e-af40-4c00-9615-02fa59358c24" />

<img width="498" height="188" alt="Снимок экрана от 2026-11-07 15-07-20" src="https://github.com/user-attachments/assets/27eddd8d-17d2-4a42-8ab6-291b7ca79cc0" />


## License

GPL-3.0
