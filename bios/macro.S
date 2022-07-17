// SPDX-License-Identifier: GPL-2.0-only
/*
 * handy BIOS macros
 */

/*
 * switch to BIOS stack
 */
.macro stack_swap
	movw %ss, %cs:(__CALLER_SS)
	movl %esp, %cs:(__CALLER_SP)
	movl %edx, %cs:(__CALLER_CLOBBER)
	movw $BIOS_SS, %dx
	movw %dx, %ss
	movw $BIOS_SP, %sp
	movl %cs:(__CALLER_CLOBBER), %edx
.endm

/*
 * restore the original stack
 */
.macro stack_restore
	movl %cs:(__CALLER_SP), %esp
	movw %cs:(__CALLER_SS), %ss
.endm
