/* Program entry for tqrun, built with -nostartfiles: the toolchain's crt1.o calls
 * __libc_start_main@GLIBC_2.34, which the Miyoo's glibc 2.28 doesn't have. This is glibc's
 * own ARM start code, bound to the old version, with an init function that runs the
 * executable's constructors (older glibc leaves that to the program). */
	.syntax unified
	.arm
	.symver __libc_start_main,__libc_start_main@GLIBC_2.4

	.text
	.globl _start
	.type _start, %function
_start:
	.fnstart
	.cantunwind
	mov fp, #0
	mov lr, #0
	pop {a2}		@ argc
	mov a3, sp		@ argv
	push {a3}		@ stack end
	push {a1}		@ rtld_fini, from the dynamic linker
	ldr ip, =tqrun_fini
	push {ip}		@ fini
	ldr a4, =tqrun_init	@ init
	ldr a1, =main
	bl __libc_start_main
	bl abort
	.fnend
	.size _start, .-_start

	.section .note.GNU-stack,"",%progbits
