/*
 * Power Management Service Unit (PMSU) support for Armada 370/XP platforms.
 *
 * Copyright (C) 2013 Marvell
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ARMADA_370_XP__PMSU_H
#define __ARMADA_370_XP__PMSU_H

void armada_370_xp_pmsu_enable_l2_powerdown_onidle(void);
void armada_370_xp_pmsu_idle_prepare(bool deepidle);
void armada_370_xp_pmsu_idle_restore(void);
void armada_370_xp_pmsu_set_start_addr(void *start_addr, int hw_cpu);

#endif	/* __ARMADA_370_XP__PMSU_H */
