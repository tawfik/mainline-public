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
#include <linux/mvebu-v7-cpuidle.h>
#include <linux/of.h>
#include <linux/suspend.h>
#include <linux/platform_device.h>

static struct mvebu_v7_cpuidle *pcpuidle;

static int mvebu_v7_enter_idle(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	int ret;
	bool deepidle = false;

	cpu_pm_enter();

	if (drv->states[index].flags & MVEBU_V7_FLAG_DEEP_IDLE)
		deepidle = true;

	ret = pcpuidle->mvebu_v7_cpu_suspend(deepidle);
	if (ret)
		return ret;

	cpu_pm_exit();

	return index;
}

static int mvebu_v7_cpuidle_probe(struct platform_device *pdev)
{
	int i;

	pcpuidle = (void *)(pdev->dev.platform_data);

	/*
	 * The first state is the ARM WFI state, so we don't have to
	 * provide an enter function
	 */
	for (i = 1; i < pcpuidle->mvebu_v7_idle_driver.state_count; i++)
		pcpuidle->mvebu_v7_idle_driver.states[i].enter =
			mvebu_v7_enter_idle;

	return cpuidle_register(&pcpuidle->mvebu_v7_idle_driver, NULL);
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
