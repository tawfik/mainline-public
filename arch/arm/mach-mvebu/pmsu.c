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
static void __iomem *pmsu_fabric_base;

/* PMSU MP registers */
#define PMSU_CONTROL_AND_CONFIG(cpu)	    ((cpu * 0x100) + 0x4)
#define PMSU_CONTROL_AND_CONFIG_DFS_REQ		BIT(18)
#define PMSU_CONTROL_AND_CONFIG_PWDDN_REQ	BIT(16)
#define PMSU_CONTROL_AND_CONFIG_L2_PWDDN	BIT(20)

#define PMSU_CPU_POWER_DOWN_CONTROL(cpu)    ((cpu * 0x100) + 0x8)

#define PMSU_CPU_POWER_DOWN_DIS_SNP_Q_SKIP	BIT(0)

#define PMSU_STATUS_AND_MASK(cpu)	    ((cpu * 0x100) + 0xc)
#define PMSU_STATUS_AND_MASK_CPU_IDLE_WAIT	BIT(16)
#define PMSU_STATUS_AND_MASK_SNP_Q_EMPTY_WAIT	BIT(17)
#define PMSU_STATUS_AND_MASK_IRQ_WAKEUP		BIT(20)
#define PMSU_STATUS_AND_MASK_FIQ_WAKEUP		BIT(21)
#define PMSU_STATUS_AND_MASK_DBG_WAKEUP		BIT(22)
#define PMSU_STATUS_AND_MASK_IRQ_MASK		BIT(24)
#define PMSU_STATUS_AND_MASK_FIQ_MASK		BIT(25)

#define PMSU_BOOT_ADDR_REDIRECT_OFFSET(cpu) ((cpu * 0x100) + 0x24)

/* PMSU reset registers */
#define PMSU_RESET_CTL_OFFSET(cpu)	    (cpu * 0x8)

/* PMSU fabric registers */
#define L2C_NFABRIC_PM_CTL		    0x4
#define L2C_NFABRIC_PM_CTL_PWR_DOWN		BIT(20)

static struct of_device_id of_pmsu_table[] = {
	{.compatible = "marvell,armada-370-xp-pmsu"},
	{ /* end of list */ },
};

void armada_370_xp_pmsu_set_start_addr(void *start_addr, int hw_cpu)
{
	writel(virt_to_phys(start_addr), pmsu_mp_base +
		PMSU_BOOT_ADDR_REDIRECT_OFFSET(hw_cpu));
}

#ifdef CONFIG_SMP
int armada_xp_boot_cpu(unsigned int cpu_id, void *boot_addr)
{
	int reg, hw_cpu;

	if (!pmsu_mp_base || !pmsu_reset_base) {
		pr_warn("Can't boot CPU. PMSU is uninitialized\n");
		return 1;
	}

	hw_cpu = cpu_logical_map(cpu_id);

	armada_370_xp_pmsu_set_start_addr(boot_addr, hw_cpu);

	/* Release CPU from reset by clearing reset bit*/
	reg = readl(pmsu_reset_base + PMSU_RESET_CTL_OFFSET(hw_cpu));
	reg &= (~0x1);
	writel(reg, pmsu_reset_base + PMSU_RESET_CTL_OFFSET(hw_cpu));

	return 0;
}
#endif

static int __init armada_370_xp_pmsu_init(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, of_pmsu_table);
	if (np) {
		pr_info("Initializing Power Management Service Unit\n");
		pmsu_mp_base = of_iomap(np, 0);
		WARN_ON(!pmsu_mp_base);
		pmsu_reset_base = of_iomap(np, 1);
		WARN_ON(!pmsu_reset_base);
		pmsu_fabric_base = of_iomap(np, 2);
		WARN_ON(!pmsu_fabric_base);
		of_node_put(np);
	}

	return 0;
}

void armada_370_xp_pmsu_enable_l2_powerdown_onidle(void)
{
	u32 reg;

	if (pmsu_fabric_base == NULL)
		return;

	/* Enable L2 & Fabric powerdown in Deep-Idle mode - Fabric */
	reg = readl(pmsu_fabric_base + L2C_NFABRIC_PM_CTL);
	reg |= L2C_NFABRIC_PM_CTL_PWR_DOWN;
	writel(reg, pmsu_fabric_base + L2C_NFABRIC_PM_CTL);
}

/* No locking is needed because we only access per-CPU registers */
void armada_370_xp_pmsu_idle_prepare(bool deepidle)
{
	unsigned int hw_cpu = cpu_logical_map(smp_processor_id());
	u32 reg;

	if (pmsu_mp_base == NULL)
		return;

	/*
	 * Adjust the PMSU configuration to wait for WFI signal, enable
	 * IRQ and FIQ as wakeup events, set wait for snoop queue empty
	 * indication and mask IRQ and FIQ from CPU
	 */
	reg = readl(pmsu_mp_base + PMSU_STATUS_AND_MASK(hw_cpu));
	reg |= PMSU_STATUS_AND_MASK_CPU_IDLE_WAIT    |
	       PMSU_STATUS_AND_MASK_IRQ_WAKEUP       |
	       PMSU_STATUS_AND_MASK_FIQ_WAKEUP       |
	       PMSU_STATUS_AND_MASK_SNP_Q_EMPTY_WAIT |
	       PMSU_STATUS_AND_MASK_IRQ_MASK         |
	       PMSU_STATUS_AND_MASK_FIQ_MASK;
	writel(reg, pmsu_mp_base + PMSU_STATUS_AND_MASK(hw_cpu));

	reg = readl(pmsu_mp_base + PMSU_CONTROL_AND_CONFIG(hw_cpu));
	/* ask HW to power down the L2 Cache if needed */
	if (deepidle)
		reg |= PMSU_CONTROL_AND_CONFIG_L2_PWDDN;

	/* request power down */
	reg |= PMSU_CONTROL_AND_CONFIG_PWDDN_REQ;
	writel(reg, pmsu_mp_base + PMSU_CONTROL_AND_CONFIG(hw_cpu));

	/* Disable snoop disable by HW - SW is taking care of it */
	reg = readl(pmsu_mp_base + PMSU_CPU_POWER_DOWN_CONTROL(hw_cpu));
	reg |= PMSU_CPU_POWER_DOWN_DIS_SNP_Q_SKIP;
	writel(reg, pmsu_mp_base + PMSU_CPU_POWER_DOWN_CONTROL(hw_cpu));
}

/* No locking is needed because we only access per-CPU registers */
static noinline void armada_370_xp_pmsu_idle_restore(void)
{
	unsigned int hw_cpu = cpu_logical_map(smp_processor_id());
	u32 reg;

	if (pmsu_mp_base == NULL)
		return;

	/* cancel ask HW to power down the L2 Cache if possible */
	reg = readl(pmsu_mp_base + PMSU_CONTROL_AND_CONFIG(hw_cpu));
	reg &= ~PMSU_CONTROL_AND_CONFIG_L2_PWDDN;
	writel(reg, pmsu_mp_base + PMSU_CONTROL_AND_CONFIG(hw_cpu));

	/* cancel Enable wakeup events and mask interrupts */
	reg = readl(pmsu_mp_base + PMSU_STATUS_AND_MASK(hw_cpu));
	reg &= ~(PMSU_STATUS_AND_MASK_IRQ_WAKEUP | PMSU_STATUS_AND_MASK_FIQ_WAKEUP);
	reg &= ~PMSU_STATUS_AND_MASK_CPU_IDLE_WAIT;
	reg &= ~PMSU_STATUS_AND_MASK_SNP_Q_EMPTY_WAIT;
	reg &= ~(PMSU_STATUS_AND_MASK_IRQ_MASK | PMSU_STATUS_AND_MASK_FIQ_MASK);
	writel(reg, pmsu_mp_base + PMSU_STATUS_AND_MASK(hw_cpu));
}

early_initcall(armada_370_xp_pmsu_init);
