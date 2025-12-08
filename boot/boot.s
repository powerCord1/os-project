.section .multiboot
.align 8
header_start:
    .long 0xE85250D6           # magic
    .long 0                    # architecture
    .long header_end - header_start # header length
    .long -(0xE85250D6 + 0 + (header_end - header_start)) # checksum

    .word 5 # type
    .word 0 # flags
    .long 24 # size
    .long 0 # min_addr
    .long 0 # max_addr
    .long 4096 # align
    .long 0 # preference

    .word 0 # type
    .word 0 # flags
    .long 8 # size
header_end:

.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:

.section .text
.global _start
.type _start, @function
_start:
	mov $stack_top, %rsp

    push %rbx
    push %rax

	call main
    call main_exit

1:	hlt
	jmp 1b

.size _start, . - _start
