bits 32
MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ 0x00000004
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

MB_VID_MODE   equ 0
MB_VID_WIDTH  equ 1920
MB_VID_HEIGHT equ 1080
MB_VID_DEPTH  equ 32

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM
    dd 0, 0, 0, 0, 0
    dd MB_VID_MODE
    dd MB_VID_WIDTH
    dd MB_VID_HEIGHT
    dd MB_VID_DEPTH

section .text
global start
extern kernel_main

start:
    cli
    mov esp, stack_top
    sub esp, 8
    push ebx
    push eax
    call kernel_main
.hang:
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits
