/*
 * Marvell Armada 370 and Armada XP SoC cpuidle driver
 *
 * Copyright (C) 2014 Marvell
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
#include <linux/of.h>
#include <linux/suspend.h>
#include <linux/platform_device.h>
#include <asm/cpuidle.h>

#define MVEBU_V7_MAX_STATES	3
#define MVEBU_V7_FLAG_DEEP_IDLE	0x10000

static int (*mvebu_v7_cpu_suspend)(int);

static int mvebu_v7_enter_idle(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	int ret;
	bool deepidle = false;
	cpu_pm_enter();

	if (drv->states[index].flags & MVEBU_V7_FLAG_DEEP_IDLE)
		deepidle = true;

	ret = mvebu_v7_cpu_suspend(deepidle);
	if (ret)
		return ret;

	cpu_pm_exit();

	return index;
}

static struct cpuidle_driver mvebu_v7_idle_driver = {
	.name			= "mvebu_v7_idle",
	.states[0]		= ARM_CPUIDLE_WFI_STATE,
	.states[1]		= {
		.enter			= mvebu_v7_enter_idle,
		.exit_latency		= 10,
		.power_usage		= 50,
		.target_residency	= 100,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "MV CPU IDLE",
		.desc			= "CPU power down",
	},
	.states[2]		= {
		.enter			= mvebu_v7_enter_idle,
		.exit_latency		= 100,
		.power_usage		= 5,
		.target_residency	= 1000,
		.flags			= CPUIDLE_FLAG_TIME_VALID |
						MVEBU_V7_FLAG_DEEP_IDLE,
		.name			= "MV CPU DEEP IDLE",
		.desc			= "CPU and L2 Fabric power down",
	},
	.state_count = MVEBU_V7_MAX_STATES,
};

static int mvebu_v7_cpuidle_probe(struct platform_device *pdev)
{

	mvebu_v7_cpu_suspend = (void *)(pdev->dev.platform_data);
	return cpuidle_register(&mvebu_v7_idle_driver, NULL);
}

static struct platform_driver mvebu_v7_cpuidle_plat_driver = {
	.driver = {
		.name = "cpuidle-mvebu-v7",
		.owner = THIS_MODULE,
	},
	.probe = mvebu_v7_cpuidle_probe,
};

module_platform_driver(mvebu_v7_cpuidle_plat_driver);

MODULE_AUTHOR("Gregory CLEMENT <gregory.clement@free-electrons.com>");
MODULE_DESCRIPTION("Mvebu v7 cpu idle driver");
MODULE_LICENSE("GPL");
