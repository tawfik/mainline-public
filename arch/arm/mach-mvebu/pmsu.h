/*
 * Power Management Service Unit (PMSU) support for Armada 370/XP platforms.
 *
 * Copyright (C) 2012 Marvell
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __MACH_MVEBU_PMSU_H
#define __MACH_MVEBU_PMSU_H

int armada_xp_boot_cpu(unsigned int cpu_id, void *phys_addr);
void armada_370_xp_pmsu_enable_l2_powerdown_onidle(void);
void armada_370_xp_pmsu_set_start_addr(void *start_addr, int hw_cpu);
int armada_370_xp_cpu_pm_init(void);

#endif	/* __MACH_370_XP_PMSU_H */
