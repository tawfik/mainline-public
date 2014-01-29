/*
 * Coherency fabric: low level functions
 *
 * Copyright (C) 2012 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * This file implements the assembly function to add a CPU to the
 * coherency fabric. This function is called by each of the secondary
 * CPUs during their early boot in an SMP kernel, this why this
 * function have to callable from assembly. It can also be called by a
 * primary CPU from C code during its boot.
 */

#include <linux/linkage.h>
#define ARMADA_XP_CFB_CTL_REG_OFFSET 0x0
#define ARMADA_XP_CFB_CFG_REG_OFFSET 0x4

#define PMSU_BOOT_ADDR_REDIRECT_OFFSET 0x124

#include <asm/assembler.h>
#include <asm/cp15.h>

	.text

	.macro modify_coherent_reg join_smp clear_coherency
	mrc	p15, 0, r1, c1, c0, 0
	tst	r1, #CR_M @ Check MMU bit enabled
	bne	1f

	/* use physical address of the coherency register */
	adr	r1, 3f
	ldr	r3, [r1]
	ldr	r1, [r1, r3]
	b	2f
1:
	/* use virtual address of the coherency register */
	ldr	r1, =coherency_base
	ldr	r1, [r1]
2:
	/* Create bit by cpu index */
	mrc	15, 0, r3, cr0, cr0, 5
	and	r3, r3, #15
	mov	r2, #(1 << 24)
	lsl	r3, r2, r3
	ARM_BE8(rev	r3, r3)

	.if \join_smp == 1
	/* Add CPU to SMP group - Atomic (only if the flag is set) */
	add	r0, r1, #ARMADA_XP_CFB_CFG_REG_OFFSET
1:
	ldrex	r2, [r0]
	orr	r2, r2, r3
	strex 	r1, r2, [r0]
	cmp	r1, #0
	bne 1b

	/* get back to the base register */
	sub	r1, r0, #ARMADA_XP_CFB_CFG_REG_OFFSET
	.endif

	/*
	 * Enable coherency on CPU - Atomic (or disable depending of
	 * the clear_coherency flag)
	 */
	add	r0, r1, #ARMADA_XP_CFB_CTL_REG_OFFSET

1:
	ldrex	r2, [r0]
	.if \clear_coherency == 1
	bic	r2, r2, r3
	.else
	orr	r2, r2, r3
	.endif
	strex	r1, r2, [r0]
	cmp	r1, #0
	bne 1b

	dsb

	mov	r0, #0
	mov	pc, lr
	.endm

.global armada370_deep_idle_exit_start
.global armada370_deep_idle_exit_end

ENTRY(armada370_deep_idle_exit_start)
armada370_deep_idle_exit_start:
	/* use physical address of the coherency register */
	adr	r1, 4f
	ldr	r3, [r1]
	ldr	r1, [r1, r3]
	add	r0, r1, #PMSU_BOOT_ADDR_REDIRECT_OFFSET
	ldr     r1, [r0]
	mov     pc, r1
armada370_deep_idle_exit_end:
ENDPROC(armada370_deep_idle_exit_end)

/*  Enable coherency on CPU */
ENTRY(ll_set_cpu_coherent)
	modify_coherent_reg join_smp = 0, clear_coherency = 0
ENDPROC(ll_set_cpu_coherent)

/*  Disable coherency on CPU */
ENTRY(ll_clear_cpu_coherent)
	modify_coherent_reg join_smp = 0, clear_coherency = 1
ENDPROC(ll_clear_cpu_coherent)

/* Add CPU to SMP group */
ENTRY(ll_set_cpu_coherent_and_smp)
	modify_coherent_reg join_smp = 1, clear_coherency = 0
ENDPROC(ll_set_cpu_coherent_and_smp)

	.align 2
3:
	.long	coherency_phys_base - .
4:
	.long	pmsu_mp_base - .
