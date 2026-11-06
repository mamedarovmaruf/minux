# minux

A educational monolithic hobby kernel for x86 (32-bit), written in C and NASM.

## Project structure

```
minux/
├── boot.asm        Entry point: Multiboot header, stack setup, call into C
├── linker.ld       Memory layout: kernel is loaded at 0x100000 (1 MB)
├── kernel.c        kernel_main(): the first code that runs in C
├── terminal.c      VGA text-mode driver implementation
├── terminal.h      VGA terminal API (colors, print functions)
├── src/main.rs     Build & run tool written in Rust (replacement for a Makefile)
├── Cargo.toml      Cargo manifest for the build tool
├── .gitignore      Ignores build artifacts (build/, target/, *.o, *.elf, ...)
└── README.md
```

### How the pieces fit together

1. The bootloader (GRUB or QEMU `-kernel`) finds the Multiboot header in `boot.asm` and jumps to `start`.
2. `start` disables interrupts, sets up a 16 KB stack, and calls `kernel_main`.
3. `kernel_main` uses the terminal API from `terminal.c` to print text to the screen.
4. `linker.ld` places the Multiboot header first, then code, read-only data, data and BSS, so the bootloader can find it.

### Terminal driver

`terminal.c` writes directly to VGA text memory at `0xB8000`. Each character takes two bytes: the ASCII code and an attribute byte (foreground and background colour). The screen is 80×25 characters.

It provides:

- `clear_screen()` clears the screen and resets the cursor.
- `set_color(fg, bg)` sets the colours for the next output.
- `print_char`, `print_string`, `print_hex` print text. They handle `\n`, `\r`, `\t` and `\b` and scroll the screen when it is full.
- The hardware cursor is moved through the VGA I/O ports `0x3D4` and `0x3D5`.

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

The build tool compiles every `.c` file in the project root, so new source files are picked up automatically. Changing any `.h` file rebuilds all `.c` files.

### Manual build (without Cargo)

```bash
nasm -f elf32 boot.asm -o boot.o
gcc -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -nostdlib -O2 -c kernel.c -o kernel.o
gcc -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -nostdlib -O2 -c terminal.c -o terminal.o
ld -m elf_i386 -z noexecstack -T linker.ld -o kernel.elf boot.o kernel.o terminal.o
qemu-system-x86_64 -kernel kernel.elf
```

## Debugging

Run `cargo run -- --debug` in one terminal, then in another:

```bash
gdb build/kernel.elf -ex 'target remote :1234'
```

## Roadmap

- [ ] GDT and IDT
- [ ] Interrupt handling and keyboard input (IRQ1)
- [ ] Memory management
- [ ] Process scheduling

## License

GPL-3.0
