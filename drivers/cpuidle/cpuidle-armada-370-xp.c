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

#include <linux/cpu_pm.h>
#include <linux/cpuidle.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/suspend.h>
#include <asm/suspend.h>
#include <linux/smp.h>
#include <asm/cpuidle.h>
#include <asm/smp_plat.h>
#include <linux/platform_device.h>
#include <asm/cp15.h>
#include <asm/cacheflush.h>
#include <linux/mbus.h>


#define CRYPT0_ENG_ID  41
#define CRYPT0_ENG_ATTR        0x1
#define SRAM_PHYS_BASE 0xFFFF0000

extern void* armada370_deep_idle_exit_end;
extern void* armada370_deep_idle_exit_start;

void a370_cpuidle_enable_wa(void)
{
	u32 code_len;
	void __iomem *sram_virt_base;

	mvebu_mbus_add_window_by_id(CRYPT0_ENG_ID, CRYPT0_ENG_ATTR,
				SRAM_PHYS_BASE, SZ_64K);
	sram_virt_base = ioremap(SRAM_PHYS_BASE, SZ_64K);

	code_len = 4 * (&armada370_deep_idle_exit_end
			- &armada370_deep_idle_exit_start);

	memcpy(sram_virt_base, &armada370_deep_idle_exit_start, code_len);
}

#define ARMADA_370_XP_MAX_STATES	3
#define ARMADA_370_XP_FLAG_DEEP_IDLE	0x10000
extern void armada_370_xp_pmsu_idle_prepare(bool deepidle);
extern void ll_clear_cpu_coherent(void);
extern void ll_set_cpu_coherent(void);

noinline static int armada_370_xp_cpu_suspend(unsigned long deepidle)
{
	armada_370_xp_pmsu_idle_prepare(deepidle);

	v7_exit_coherency_flush(all);

//	ll_clear_cpu_coherent();

//	dsb();

	wfi();

//	ll_set_cpu_coherent();

	/* Re-enable C-bit if needed */
	asm volatile(
	"mrc	p15, 0, %0, c1, c0, 0 \n\t"
	"tst	%0, #(1 << 2) \n\t"
	"orreq	r0, %0, #(1 << 2) \n\t"
	"mcreq	p15, 0, %0, c1, c0, 0 \n\t"
	"isb	"
	: : "r" (0));

	return 0;
}

static int armada_370_xp_enter_idle(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	bool deepidle = false;
	cpu_pm_enter();

	/*
	 * Armada 370 has single power domain so the L2 power down
	 * request bit must be set in any case
	 */
	if (drv->states[index].flags & ARMADA_370_XP_FLAG_DEEP_IDLE ||
		of_machine_is_compatible("marvell,armada370"))
		deepidle = true;

	cpu_suspend(deepidle, armada_370_xp_cpu_suspend);

	cpu_pm_exit();

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
		.flags			= CPUIDLE_FLAG_TIME_VALID |
						ARMADA_370_XP_FLAG_DEEP_IDLE,
		.name			= "MV CPU DEEP IDLE",
		.desc			= "CPU and L2 Fabric power down",
	},
	.state_count = ARMADA_370_XP_MAX_STATES,
};

static int armada_370_xp_cpuidle_probe(struct platform_device *pdev)
{
	a370_cpuidle_enable_wa();
	return cpuidle_register(&armada_370_xp_idle_driver, NULL);
}

static struct platform_driver armada_370_xp_cpuidle_plat_driver = {
	.driver = {
		.name = "cpuidle-armada-370-xp",
		.owner = THIS_MODULE,
	},
	.probe = armada_370_xp_cpuidle_probe,
};

module_platform_driver(armada_370_xp_cpuidle_plat_driver);

MODULE_AUTHOR("Gregory CLEMENT <gregory.clement@free-electrons.com>");
MODULE_DESCRIPTION("Armada 370/XP cpu idle driver");
MODULE_LICENSE("GPL");
