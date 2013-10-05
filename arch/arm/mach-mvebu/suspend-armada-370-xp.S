/*
 * CPU idle low level implementation for Marvell Armada 370 and Armada XP SoCs
 *
 * Copyright (C) 2013 Marvell
 *
 * Nadav Haklai <nadavh@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 */
#include <linux/linkage.h>

/*
* armadaxp_cpu_suspend: enter cpu deepIdle state
* input:
*/
ENTRY(armada_370_xp_cpu_suspend)
/* Save ARM registers */
	stmfd	sp!, {r4 - r11, lr}	@ save registers on stack

	bl armada_370_xp_pmsu_idle_prepare
	/*
	 * Invalidate L1 data cache. Even though only invalidate is
	 * necessary exported flush API is used here. Doing clean
	 * on already clean cache would be almost NOP.
	 */
	bl v7_flush_dcache_all

	/*
	 * Clear the SCTLR.C bit to prevent further data cache
	 * allocation. Clearing SCTLR.C would make all the data accesses
	 * strongly ordered and would not hit the cache.
	 */
	mrc	p15, 0, r0, c1, c0, 0
	bic	r0, r0, #(1 << 2)	@ Disable the C bit
	mcr	p15, 0, r0, c1, c0, 0
	isb

	bl v7_flush_dcache_all

	/* Data memory barrier and Data sync barrier */
	dsb
	dmb

	bl armada_370_xp_disable_snoop_ena

	dsb				@ Data Synchronization Barrier

/*
 * ===================================
 * == WFI instruction => Enter idle ==
 * ===================================
 */

	wfi				@ wait for interrupt
/*
 * ===================================
 * == Resume path for non-OFF modes ==
 * ===================================
 */

/* Enable SnoopEna - Exclusive */
	mov	r0, #1			@ r0!=0 means use virtual address
	mov	r1, #0			@ Do not add CPU to SMP group
	bl ll_set_cpu_coherent

/* Re-enable C-bit if needed */
	mrc	p15, 0, r0, c1, c0, 0
	tst	r0, #(1 << 2)		@ Check C bit enabled?
	orreq	r0, r0, #(1 << 2)	@ Enable the C bit if cleared
	mcreq	p15, 0, r0, c1, c0, 0
	isb

	ldmfd	sp!, {r4 - r11, pc}	@ restore regs and return
ENDPROC(armada_370_xp_cpu_suspend)

/*
* armada_370_xp_cpu_resume: exit cpu deepIdle state
*/
ENTRY(armada_370_xp_cpu_resume)
	mov	r0, #0			@ r0==0 means use physical address
	mov	r1, #1			@ Add CPU to SMP group
	bl ll_set_cpu_coherent

	/* Now branch to the common CPU resume function */
	b	cpu_resume

ENDPROC(armada_370_xp_cpu_resume)
