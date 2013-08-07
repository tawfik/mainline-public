/*
 * Marvell Armada 370 and Armada XP SoC cpuidle driver
 *
 * Copyright (C) 2013 Marvell
 *
 * Nadav Haklai <nadavh@marvell.com>
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * Maintainer: Gregory CLEMENT <gregory.clement@free-electrons.com>
 */

#include <linux/cpuidle.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/suspend.h>
#include <asm/suspend.h>
#include <linux/smp.h>
#include <asm/cpuidle.h>
#include <asm/smp_plat.h>
#include <linux/armada-370-xp-pmsu.h>

#define ARMADA_370_XP_MAX_STATES	3

enum mv_pm_states {
	WFI = 0,
	MV_CPU_IDLE,
	MV_CPU_DEEP_IDLE,
};

extern void v7_flush_dcache_all(void);

/* Functions defined in suspend-armada-370-xp.S */
int armada_370_xp_cpu_resume(unsigned long);
int armada_370_xp_cpu_suspend(unsigned long);

static int armada_370_xp_enter_idle(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	bool deepidle = false;
	unsigned int hw_cpu = cpu_logical_map(smp_processor_id());

	armada_370_xp_pmsu_set_start_addr(armada_370_xp_cpu_resume, hw_cpu);

	if (index == MV_CPU_DEEP_IDLE)
		deepidle = true;

	cpu_suspend(deepidle, armada_370_xp_cpu_suspend);

	cpu_init();

	armada_370_xp_pmsu_idle_restore();

	return index;
}

static struct cpuidle_driver armada_370_xp_idle_driver = {
	.name			= "armada_370_xp_idle",
	.states[0]		= ARM_CPUIDLE_WFI_STATE,
	.states[1]		= {
		.enter			= armada_370_xp_enter_idle,
		.exit_latency		= 10,
		.power_usage		= 50,
		.target_residency	= 100,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "MV CPU IDLE",
		.desc			= "CPU power down",
	},
	.states[2]		= {
		.enter			= armada_370_xp_enter_idle,
		.exit_latency		= 100,
		.power_usage		= 5,
		.target_residency	= 1000,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "MV CPU DEEP IDLE",
		.desc			= "CPU and L2 Fabric power down",
	},
	.state_count = ARMADA_370_XP_MAX_STATES,
};

static int __init armada_370_xp_cpuidle_init(void)
{
	if (!of_find_compatible_node(NULL, NULL, "marvell,armada-370-xp-pmsu"))
		return -ENODEV;

	if (!of_find_compatible_node(NULL, NULL, "marvell,coherency-fabric"))
		return -ENODEV;

	pr_info("Initializing Armada-XP CPU power management ");

	armada_370_xp_pmsu_enable_l2_powerdown_onidle();

	return cpuidle_register(&armada_370_xp_idle_driver, NULL);
}

module_init(armada_370_xp_cpuidle_init);

MODULE_LICENSE("GPL");
