# minux

An educational monolithic hobby kernel for x86 (32-bit), written in C and NASM.

Minux boots in QEMU or on real hardware via Multiboot, detects the screen resolution and draws its console on a linear framebuffer (1920x1080 by default), sets up its own interrupt handling, reads the PS/2 keyboard and runs a small command shell called MOS.

## Project structure

```
minux/
├── boot.asm        Entry point: Multiboot header (with video mode request), stack setup, call into C
├── isr.asm         CPU exception stubs (0-31) and IRQ stubs (32-47)
├── linker.ld       Memory layout: kernel is loaded at 0x100000 (1 MB)
├── kernel.c        kernel_main(): hardware init, then hands control to the shell
├── idt.c / idt.h   Interrupt Descriptor Table setup
├── pic.c / pic.h   8259 PIC remapping, EOI, IRQ mask/unmask
├── irq.c / irq.h   IRQ dispatcher and handler registration
├── io.h            inb / outb / inw / outw / inl / outl / io_wait port I/O helpers
├── panic.c / panic.h   Exception handler and kernel panic screen
├── video.c / video.h         Display detection (Multiboot / Bochs VBE) and framebuffer drawing
├── font8x16.h      8x16 bitmap font (256 glyphs, CP437 layout)
├── terminal.c / terminal.h   Text console on top of the framebuffer, VGA text mode as fallback
├── keyboard.c / keyboard.h   PS/2 keyboard driver (scancode set 1)
├── mos.c / mos.h   MOS shell: line editing, parser, built-in commands
├── kstring.c / kstring.h   Minimal string functions (no libc in the kernel)
├── src/main.rs     Build & run tool written in Rust (replacement for a Makefile)
├── Cargo.toml      Cargo manifest for the build tool
└── README.md
```

## How the pieces fit together

1. The bootloader (GRUB or QEMU `-kernel`) finds the Multiboot header in `boot.asm` and jumps to `start`.
2. `start` sets up the stack and calls `kernel_main(magic, mbi)`, passing the Multiboot magic (`EAX`) and the Multiboot info pointer (`EBX`).
3. `kernel_main` runs the initialisation sequence:
   - `video_init()` detects the display and `terminal_init()` switches the console to the framebuffer if one was found.
   - `pic_init()` remaps the two 8259 PICs so that IRQ0-15 land on vectors 32-47 instead of colliding with CPU exceptions.
   - `idt_init()` fills the IDT: vectors 0-31 point to exception stubs, vectors 32-47 point to IRQ stubs.
   - `keyboard_init()` drains the controller buffer, registers the keyboard IRQ handler and unmasks IRQ1.
   - `sti` enables hardware interrupts.
4. `kernel_main` calls `mos_run()`, which reads keys through `keyboard_getchar()` and executes commands.

Interrupt path: a hardware IRQ enters an `irqN` stub in `isr.asm`, which pushes a register frame and calls `irq_handler()`. The dispatcher calls the registered handler (for example `keyboard_irq()`) and sends EOI to the PIC.

CPU exceptions take the same route through `isr_common` and `isr_handler()`, which prints a panic screen with the register dump. Page faults also print CR2.

## Components

### Video and terminal

The display is detected once at boot by `video_init()`, in this order:

1. **Multiboot framebuffer.** The header in `boot.asm` asks the bootloader for a 1920x1080, 32 bpp linear mode. GRUB sets it up and describes the mode it actually chose in the Multiboot info structure; the kernel uses whatever it gets (32, 24, 16 and 15 bpp RGB are supported).
2. **Bochs VBE (BGA).** QEMU's `-kernel` loader ignores the video mode request, so if the Multiboot info has no framebuffer the kernel programs the Bochs VBE registers (ports `0x1CE` / `0x1CF`) itself. It reads the device maximum and picks the first mode that fits from 1920x1080, 1600x900, 1280x720, 1024x768 and 800x600. The framebuffer address is read from PCI BAR0. This works in QEMU (`-vga std`), Bochs and VirtualBox.
3. **VGA text mode** at `0xB8000`, 80x25, if neither of the above is available.

`terminal.c` draws characters with the 8x16 bitmap font in `font8x16.h` and keeps every cell in a shadow buffer, which is used for the software cursor and for scrolling. The size of the text grid depends on the mode: 240x67 cells on 1920x1080 with the default 1x font. The font size is chosen automatically (1x up to 2560x1440, 2x on 4K) and can be forced with `TERM_FONT_SCALE_X2` at the top of `terminal.c` (`2` = 1x, `3` = 1.5x, `4` = 2x, `0` = automatic).

It provides `clear_screen()`, `set_color(fg, bg)`, `print_char`, `print_string`, `print_hex` and `print_dec`. The control characters `\n`, `\r`, `\t` and `\b` are handled, and the screen scrolls when full. `term_cols()` and `term_rows()` return the current grid size, so code such as the editor and the panic screen adapts to any resolution. In VGA text mode the hardware cursor is moved through the ports `0x3D4` and `0x3D5`.

The colours are the 16 standard VGA colours in both modes.

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
| `video`  | show the current video mode, framebuffer address and text grid size |
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

QEMU is started with `-vga std`, which provides the Bochs VBE registers used to switch to 1920x1080.

The build tool compiles every `.asm` and `.c` file in the project root and links them with `linker.ld`. New source files are picked up automatically. Changing a `.h` file rebuilds all `.c` files. The tool also checks that `nasm`, `gcc` or `clang`, `ld` and `qemu-system-x86_64` are installed and prints the apt command if something is missing. Set `CC=clang` to use clang, and `NO_COLOR=1` to disable coloured output.

### Manual build (without Cargo)

```bash
mkdir -p build
for f in *.asm; do nasm -f elf32 "$f" -o "build/${f%.asm}.asm.o"; done
for f in *.c; do gcc -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -nostdlib -O2 -Wall -Wextra -c "$f" -o "build/${f%.c}.o"; done
ld -m elf_i386 -z noexecstack -T linker.ld -o build/kernel.elf build/*.o
qemu-system-x86_64 -kernel build/kernel.elf -vga std
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
- [x] Framebuffer console with automatic resolution detection (Multiboot / Bochs VBE)
- [ ] Own GDT
- [ ] Timer (IRQ0) and `uptime` command
- [ ] Command history and arrow keys (decode `0xE0` sequences)
- [ ] Function keys and extended keys
- [ ] Memory management (physical allocator, paging)
- [ ] Process scheduling

## Screenshots

<img width="713" height="408" alt="Снимок экрана от 2026-11-08 17-09-50" src="https://github.com/user-attachments/assets/4ea63fd7-ddb8-4f3b-ad74-d54257e374c7" />
<img width="1920" height="1080" alt="Снимок экрана от 2026-11-08 21-22-23" src="https://github.com/user-attachments/assets/27bc5650-312d-4370-afaa-f8a8d716690f" />
<img width="724" height="401" alt="panic" src="https://github.com/user-attachments/assets/c0836839-9761-471d-a732-5fb64a93553e" />


## License

GPL-3.0

`font8x16.h` is generated from GNU Unifont (SIL OFL 1.1).
