/*
 * Marvell EBU cpuidle defintion
 *
 * Copyright (C) 2014 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 */

#ifndef __LINUX_MVEBU_V7_CPUIDLE_H__
#define __LINUX_MVEBU_V7_CPUIDLE_H__

#define MVEBU_V7_FLAG_DEEP_IDLE	0x10000

struct mvebu_v7_cpuidle {
	struct cpuidle_driver mvebu_v7_idle_driver;
	int (*mvebu_v7_cpu_suspend)(unsigned long);
};

#endif
