/*
 * Power Management Service Unit(PMSU) support for Armada 370/XP platforms.
 *
 * Copyright (C) 2012 Marvell
 *
 * Yehuda Yitschak <yehuday@marvell.com>
 * Gregory Clement <gregory.clement@free-electrons.com>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * The Armada 370 and Armada XP SOCs have a power management service
 * unit which is responsible for powering down and waking up CPUs and
 * other SOC units
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/smp.h>
#include <asm/smp_plat.h>
#include "pmsu.h"

static void __iomem *pmsu_mp_base;
static void __iomem *pmsu_reset_base;

#define PMSU_BASE_OFFSET    0x100
#define PMSU_REG_SIZE	    0x1000

#define PMSU_BOOT_ADDR_REDIRECT_OFFSET(cpu)	((cpu * 0x100) + 0x124)
#define PMSU_RESET_CTL_OFFSET(cpu)		(cpu * 0x8)

static struct of_device_id of_pmsu_table[] = {
	{
		.compatible = "marvell,armada-370-pmsu",
		.data = (void *) false,
	},
	{
		.compatible = "marvell,armada-370-xp-pmsu",
		.data = (void *) true, /* legacy */
	},
	{ /* end of list */ },
};

#ifdef CONFIG_SMP
int armada_xp_boot_cpu(unsigned int cpu_id, void *boot_addr)
{
	int reg, hw_cpu;

	if (!pmsu_mp_base || !pmsu_reset_base) {
		pr_warn("Can't boot CPU. PMSU is uninitialized\n");
		return 1;
	}

	hw_cpu = cpu_logical_map(cpu_id);

	writel(virt_to_phys(boot_addr), pmsu_mp_base +
			PMSU_BOOT_ADDR_REDIRECT_OFFSET(hw_cpu));

	/* Release CPU from reset by clearing reset bit*/
	reg = readl(pmsu_reset_base + PMSU_RESET_CTL_OFFSET(hw_cpu));
	reg &= (~0x1);
	writel(reg, pmsu_reset_base + PMSU_RESET_CTL_OFFSET(hw_cpu));

	return 0;
}
#endif

static void __init armada_370_xp_pmsu_legacy_init(struct device_node *np)
{
	u32 addr;
	pr_warn("*** Warning ***  Using an old binding which will be deprecated\n");
	/* We just need the adress, we already know the size */
	addr = be32_to_cpu(*of_get_address(np, 0, NULL, NULL));
	addr -= PMSU_BASE_OFFSET;
	pmsu_mp_base = ioremap(addr, PMSU_REG_SIZE);
	of_node_put(np);
}

static int __init armada_370_xp_pmsu_init(void)
{
	struct device_node *np;
	np = of_find_matching_node(NULL, of_pmsu_table);
	if (np) {
		const struct of_device_id *match =
			of_match_node(of_pmsu_table, np);
		BUG_ON(!match);

		pr_info("Initializing Power Management Service Unit\n");

		if (match->data) /* legacy */
			armada_370_xp_pmsu_legacy_init(np);
		else
			pmsu_mp_base = of_iomap(np, 0);
		WARN_ON(!pmsu_mp_base);
		of_node_put(np);

		/*
		 * This temporaty hack will be removed as soon as we
		 * get the proper reset controler support
		 */
		np = of_find_compatible_node(NULL, NULL, "marvell,armada-xp-cpu-reset");
		pmsu_reset_base = of_iomap(np, 0);
		WARN_ON(!pmsu_reset_base);
		of_node_put(np);
	}

	return 0;
}

early_initcall(armada_370_xp_pmsu_init);
