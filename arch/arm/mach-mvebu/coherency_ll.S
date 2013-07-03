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

	.text
/*
 * r0: Use virtual address if r0 != 0, else physical address
 * r1: Add CPU to the SMP group if r1 != 0
 */
ENTRY(ll_set_cpu_coherent)
	cmp	r0, #0
	bne	1f
    /* use physical address */
	adr	r0, 3f
	ldr	r3, [r0]
	ldr	r0, [r0, r3]
	b	2f
1:
    /* use virtual address */
	ldr	r0, =(coherency_base)
	ldr	r0, [r0]
2:
	/* Create bit by cpu index */
	mrc	15, 0, r3, cr0, cr0, 5
	and	r3, r3, #15
	mov	r2, #(1 << 24)
	lsl	r3, r2, r3

	/* If r1=!0 then just enable the snooping */
	cmp	r1, #0
	beq	2f

	/* Add CPU to SMP group - Atomic */
	add	r1, r0, #ARMADA_XP_CFB_CTL_REG_OFFSET
1:
	ldrex	r2, [r1]
	orr	r2, r2, r3
	strex 	r0, r2, [r1]
	cmp	r0, #0
	bne 1b

2:
	/* Enable coherency on CPU - Atomic */
	add	r1, r1, #ARMADA_XP_CFB_CFG_REG_OFFSET
1:
	ldrex	r2, [r1]
	orr	r2, r2, r3
	strex	r0, r2, [r1]
	cmp	r0, #0
	bne 1b

	dsb

	mov	r0, #0
	mov	pc, lr
ENDPROC(ll_set_cpu_coherent)

/*
 * r0: if r0==0 => physical addres, else virtual address
 */
ENTRY(armada_370_xp_disable_snoop_ena)
	ldr	r0, =(coherency_base)
	ldr	r0, [r0]
	/* Enable SnoopEna - Exclusive */
	mrc	15, 0, r1, cr0, cr0, 5
	and	r1, r1, #15
	mov	r2, #(1 << 24)
	lsl	r2, r2, r1

1:
	ldrex	r1, [r0]
	bic	r1, r1, r2
	strex	r3, r1, [r0]
	cmp	r3, #0
	bne 1b

	mov pc, lr
ENDPROC(armada_370_xp_disable_snoop_ena)

	.align 2
3:
		.long	coherency_phys_base - .
