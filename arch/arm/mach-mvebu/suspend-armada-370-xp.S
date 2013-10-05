/*
 * CPU idle low level implementation for Marvell Armada XP SoCs
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
 * armada_370_xp_cpu_resume: exit cpu deepIdle state (exiting from OFF mode)
 */
ENTRY(armada_370_xp_cpu_resume)
	bl	ll_set_cpu_coherent_and_smp

	/* Now branch to the common CPU resume function */
	b	cpu_resume

ENDPROC(armada_370_xp_cpu_resume)
