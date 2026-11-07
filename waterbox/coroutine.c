/* coroutine.c - one stack for the game, one for whoever calls the core.
 *
 * SDLPoP is written the way the DOS game was: its main loop owns the machine
 * and waits for timers inside it. A core is called one step at a time. So the
 * game runs on a stack of its own and hands control back where it would have
 * waited; the next FrameAdvance hands it control again. Nothing is threaded:
 * one switch out, one switch in, both on the caller's thread.
 *
 * The switch saves exactly what the CPU's calling convention says a callee
 * must preserve (on x86-64 rbx, rbp, r12-r15, the MXCSR and the x87 control
 * word; on aarch64 x19-x30, d8-d15 and the FPCR - x18 is the platform's, and
 * the guest never touches it) on the stack being left, and stores that stack
 * pointer. Everything else a suspended game holds is on its own stack, which
 * is guest memory like any other, so a savestate taken between two steps
 * carries the whole suspended game.
 *
 * The stack is asked for with MAP_STACK: miniBox tracks writes by protecting
 * pages, and a page the stack pointer is in has to be handled as a stack or a
 * write fault on it cannot be reported (chimera-core-ares found this; see
 * miniBox's memblock.c). Natively the flag costs nothing.
 */
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include "coroutine.h"

#ifndef MAP_STACK
#define MAP_STACK 0x20000
#endif

#if defined(__x86_64__)
__asm__(
	".text\n"
	".globl chimera_co_switch\n"
	".type chimera_co_switch,@function\n"
	"chimera_co_switch:\n"          /* void chimera_co_switch(void **save_sp, void *load_sp) */
	"	pushq %rbp\n"
	"	pushq %rbx\n"
	"	pushq %r12\n"
	"	pushq %r13\n"
	"	pushq %r14\n"
	"	pushq %r15\n"
	"	subq $8, %rsp\n"
	"	stmxcsr (%rsp)\n"
	"	fnstcw 4(%rsp)\n"
	"	movq %rsp, (%rdi)\n"
	"	movq %rsi, %rsp\n"
	"	ldmxcsr (%rsp)\n"
	"	fldcw 4(%rsp)\n"
	"	addq $8, %rsp\n"
	"	popq %r15\n"
	"	popq %r14\n"
	"	popq %r13\n"
	"	popq %r12\n"
	"	popq %rbx\n"
	"	popq %rbp\n"
	"	ret\n"
	".size chimera_co_switch, .-chimera_co_switch\n"
	"\n"
	".globl chimera_co_boot\n"
	".type chimera_co_boot,@function\n"
	"chimera_co_boot:\n"            /* the first switch into a new stack returns here */
	"	andq $-16, %rsp\n"
	"	call chimera_co_entry\n"
	"	ud2\n"
	".size chimera_co_boot, .-chimera_co_boot\n");
#elif defined(__aarch64__)
__asm__(
	".text\n"
	".globl chimera_co_switch\n"
	".type chimera_co_switch,%function\n"
	"chimera_co_switch:\n"            /* void chimera_co_switch(void **save_sp, void *load_sp) */
	"	sub sp, sp, #176\n"
	"	stp x19, x20, [sp, #0]\n"
	"	stp x21, x22, [sp, #16]\n"
	"	stp x23, x24, [sp, #32]\n"
	"	stp x25, x26, [sp, #48]\n"
	"	stp x27, x28, [sp, #64]\n"
	"	stp x29, x30, [sp, #80]\n"
	"	stp d8, d9, [sp, #96]\n"
	"	stp d10, d11, [sp, #112]\n"
	"	stp d12, d13, [sp, #128]\n"
	"	stp d14, d15, [sp, #144]\n"
	"	mrs x9, fpcr\n"
	"	str x9, [sp, #160]\n"
	"	mov x9, sp\n"
	"	str x9, [x0]\n"
	"	mov sp, x1\n"
	"	ldr x9, [sp, #160]\n"
	"	msr fpcr, x9\n"
	"	ldp x19, x20, [sp, #0]\n"
	"	ldp x21, x22, [sp, #16]\n"
	"	ldp x23, x24, [sp, #32]\n"
	"	ldp x25, x26, [sp, #48]\n"
	"	ldp x27, x28, [sp, #64]\n"
	"	ldp x29, x30, [sp, #80]\n"
	"	ldp d8, d9, [sp, #96]\n"
	"	ldp d10, d11, [sp, #112]\n"
	"	ldp d12, d13, [sp, #128]\n"
	"	ldp d14, d15, [sp, #144]\n"
	"	add sp, sp, #176\n"
	"	ret\n"
	".size chimera_co_switch, .-chimera_co_switch\n"
	"\n"
	".globl chimera_co_boot\n"
	".type chimera_co_boot,%function\n"
	"chimera_co_boot:\n"              /* the first switch into a new stack returns here */
	"	bl chimera_co_entry\n"
	"	udf #0\n"
	".size chimera_co_boot, .-chimera_co_boot\n");
#else
#error "coroutines are written for x86-64 and aarch64"
#endif

void chimera_co_boot(void);

void *chimera_co_create(size_t size)
{
	size = (size + 4095) & ~(size_t)4095;
	void *base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
	if (base == MAP_FAILED)
		return NULL;

#if defined(__x86_64__)
	/* A frame chimera_co_switch can "return" through: the saved control words,
	 * six zeroed callee-saved registers, and chimera_co_boot as the return
	 * address, with one slot of padding above it so that boot starts with the
	 * stack as a call would have left it. */
	uint64_t *sp = (uint64_t *)((uint8_t *)base + size);
	*--sp = 0;
	*--sp = (uint64_t)(uintptr_t)chimera_co_boot;
	for (int i = 0; i < 6; i++)
		*--sp = 0;
	uint32_t mxcsr = __builtin_ia32_stmxcsr();
	uint16_t fpucw;
	__asm__ volatile("fnstcw %0" : "=m"(fpucw));
	uint64_t words = (uint64_t)mxcsr | ((uint64_t)fpucw << 32);
	*--sp = words;
#else
	/* The 176-byte frame chimera_co_switch restores: every register zero but
	 * x30, the link register, which "returns" into chimera_co_boot, and the
	 * FPCR, the one this thread has now. The stack top stays 16-byte
	 * aligned, as aarch64 requires. */
	uint64_t *sp = (uint64_t *)((uint8_t *)base + size) - 22;
	for (int i = 0; i < 22; i++) sp[i] = 0;
	sp[11] = (uint64_t)(uintptr_t)chimera_co_boot;
	uint64_t fpcr;
	__asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
	sp[20] = fpcr;
#endif
	return sp;
}
