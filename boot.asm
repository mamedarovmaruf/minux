[bits 32]
global start
extern kernel_main 

section .text
start:
cli
call kernel_main
hlt