/*
 * Copyright (C) 2013 Marvell
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define pr_fmt(fmt) "mvebu-cpureset: " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/resource.h>
#include "armada-370-xp.h"

static struct of_device_id of_cpu_reset_table[] = {
	{.compatible = "marvell,armada-370-cpu-reset", .data = (void*) ARMADA_370_MAX_CPUS },
	{.compatible = "marvell,armada-xp-cpu-reset",  .data = (void*) ARMADA_XP_MAX_CPUS },
	{ /* end of list */ },
};

static void __iomem *cpu_reset_base;
static int ncpus;

#define CPU_RESET_OFFSET(cpu) (cpu * 0x8)
#define CPU_RESET_ASSERT      BIT(0)

int mvebu_cpu_reset_deassert(int cpu)
{
	u32 reg;

	if (cpu >= ncpus)
		return -EINVAL;

	if (!cpu_reset_base)
		return -ENODEV;

	reg = readl(cpu_reset_base + CPU_RESET_OFFSET(cpu));
	reg &= ~CPU_RESET_ASSERT;
	writel(reg, cpu_reset_base + CPU_RESET_OFFSET(cpu));

	return 0;
}

static int __init mvebu_cpu_reset_init(void)
{
	struct device_node *np;
	const struct of_device_id *match;
	struct resource res;
	int ret = 0;

	np = of_find_matching_node_and_match(NULL, of_cpu_reset_table,
					     &match);
	if (!np)
		return 0;

	if (of_address_to_resource(np, 0, &res)) {
		pr_err("unable to get resource\n");
		ret = -ENOENT;
		goto out;
	}

	if (!request_mem_region(res.start, resource_size(&res),
				np->full_name)) {
		pr_err("unable to request region\n");
		ret = -EBUSY;
		goto out;
	}

	cpu_reset_base = ioremap(res.start, resource_size(&res));
	if (!cpu_reset_base) {
		pr_err("unable to map registers\n");
		release_mem_region(res.start, resource_size(&res));
		ret = -ENOMEM;
		goto out;
	}

	ncpus = (int) match->data;

out:
	of_node_put(np);
	return ret;
}

early_initcall(mvebu_cpu_reset_init);
