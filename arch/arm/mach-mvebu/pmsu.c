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

#define pr_fmt(fmt) "mvebu-pmsu: " fmt

#include <linux/cpu_pm.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/smp.h>
#include <linux/resource.h>
#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/smp_plat.h>
#include <asm/suspend.h>
#include <asm/tlbflush.h>
#include "common.h"

#include <linux/mbus.h>


#define CRYPT0_ENG_ID  0x9
#define CRYPT0_ENG_ATTR        0x1
#define SRAM_PHYS_BASE 0xFFFF0000

extern void* armada370_deep_idle_exit_end;
extern void* armada370_deep_idle_exit_start;

static unsigned long pmsu_mp_phys_base;

void a370_cpuidle_enable_wa(void)
{
	u32 code_len;
	void __iomem *sram_virt_base;
	mvebu_mbus_del_window(0xfff00000 , SZ_1M);
	mvebu_mbus_add_window_by_id(CRYPT0_ENG_ID, CRYPT0_ENG_ATTR,
				SRAM_PHYS_BASE, SZ_64K);
	sram_virt_base = ioremap(SRAM_PHYS_BASE, SZ_64K);

	code_len = 4 * (&armada370_deep_idle_exit_end
			- &armada370_deep_idle_exit_start);

	memcpy(sram_virt_base, &armada370_deep_idle_exit_start, code_len);
	*(unsigned long *)(sram_virt_base + code_len - 4) = pmsu_mp_phys_base;
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 1, 4, sram_virt_base,
		code_len, 0);
}

static void __iomem *pmsu_mp_base;
static void __iomem *pmu_base;

#define PMSU_BASE_OFFSET    0x100
#define PMSU_REG_SIZE	    0x1000

/* PMSU MP registers */
#define PMSU_CONTROL_AND_CONFIG(cpu)	    ((cpu * 0x100) + 0x104)
#define PMSU_CONTROL_AND_CONFIG_DFS_REQ		BIT(18)
#define PMSU_CONTROL_AND_CONFIG_PWDDN_REQ	BIT(16)
#define PMSU_CONTROL_AND_CONFIG_L2_PWDDN	BIT(20)

#define PMSU_CPU_POWER_DOWN_CONTROL(cpu)    ((cpu * 0x100) + 0x108)

#define PMSU_CPU_POWER_DOWN_DIS_SNP_Q_SKIP	BIT(0)

#define PMSU_STATUS_AND_MASK(cpu)	    ((cpu * 0x100) + 0x10c)
#define PMSU_STATUS_AND_MASK_CPU_IDLE_WAIT	BIT(16)
#define PMSU_STATUS_AND_MASK_SNP_Q_EMPTY_WAIT	BIT(17)
#define PMSU_STATUS_AND_MASK_IRQ_WAKEUP		BIT(20)
#define PMSU_STATUS_AND_MASK_FIQ_WAKEUP		BIT(21)
#define PMSU_STATUS_AND_MASK_DBG_WAKEUP		BIT(22)
#define PMSU_STATUS_AND_MASK_IRQ_MASK		BIT(24)
#define PMSU_STATUS_AND_MASK_FIQ_MASK		BIT(25)

#define PMSU_BOOT_ADDR_REDIRECT_OFFSET(cpu) ((cpu * 0x100) + 0x124)

/* PMSU fabric registers */
#define L2C_NFABRIC_PM_CTL		    0x4
#define L2C_NFABRIC_PM_CTL_PWR_DOWN		BIT(20)


/* PMU registers */
#define PMU_PWR_IF_POLARITY	0x4
#define PMU_PWR_POLARITY_0	    BIT(0)
#define PMU_PWR_POLARITY_1	    BIT(1)
#define PMU_PWR_POLARITY_2	    BIT(2)

#define PMU_PWR_UP_DELAY_0	0x14

extern void ll_disable_coherency(void);
extern void ll_enable_coherency(void);

static struct platform_device armada_xp_cpuidle_device = {
	.name = "cpuidle-armada-370-xp",
};

static struct of_device_id of_pmsu_table[] = {
	{ .compatible = "marvell,armada-370-pmsu", },
	{ .compatible = "marvell,armada-370-xp-pmsu", },
	{ .compatible = "marvell,armada-380-pmsu", },
	{ /* end of list */ },
};

static struct of_device_id of_pmu_table[] = {
	{ .compatible = "marvell,armada-370-pmu", },
	{ /* end of list */ },
};

void mvebu_pmsu_set_cpu_boot_addr(int hw_cpu, void *boot_addr)
{
	writel(virt_to_phys(boot_addr), pmsu_mp_base +
		PMSU_BOOT_ADDR_REDIRECT_OFFSET(hw_cpu));
}

static int __init armada_370_xp_pmsu_init(void)
{
	struct device_node *np;
	struct resource res;
	int ret = 0;

	np = of_find_matching_node(NULL, of_pmsu_table);
	if (!np)
		return 0;

	pr_info("Initializing Power Management Service Unit\n");

	if (of_address_to_resource(np, 0, &res)) {
		pr_err("unable to get resource\n");
		ret = -ENOENT;
		goto out;
	}

	if (of_device_is_compatible(np, "marvell,armada-370-xp-pmsu")) {
		pr_warn(FW_WARN "deprecated pmsu binding\n");
		res.start = res.start - PMSU_BASE_OFFSET;
		res.end = res.start + PMSU_REG_SIZE - 1;
	}

	if (!request_mem_region(res.start, resource_size(&res),
				np->full_name)) {
		pr_err("unable to request region\n");
		ret = -EBUSY;
		goto out;
	}

	pmsu_mp_phys_base = res.start;

	pmsu_mp_base = ioremap(res.start, resource_size(&res));
	if (!pmsu_mp_base) {
		pr_err("unable to map registers\n");
		release_mem_region(res.start, resource_size(&res));
		ret = -ENOMEM;
		goto out;
	}

       np = of_find_matching_node(NULL, of_pmu_table);
       if (np) {
	       pr_info("Initializing Power Management Unit\n");
	       pmu_base = of_iomap(np, 0);
	       WARN_ON(!pmu_base);
       }
       pr_err("%s PMU_PWR_IF_POLARITY=%X\n",__func__, readl(pmu_base +PMU_PWR_IF_POLARITY) );
       pr_err("%s PMU_PWR_UP_DELAY_0=%X\n",__func__, readl(pmu_base +PMU_PWR_UP_DELAY_0) );

       writel(0, pmu_base +PMU_PWR_IF_POLARITY);
       writel(0x5e80, pmu_base + PMU_PWR_UP_DELAY_0);

       {
	       int i = 0;
	       for (i = 0; i< 0x28; i+=4)
		       printk("@%X = %X\n", i, readl(pmu_base +i));
       }
       pr_err("%s PMU_PWR_IF_POLARITY=%X\n",__func__, readl(pmu_base +PMU_PWR_IF_POLARITY) );
       pr_err("%s PMU_PWR_UP_DELAY_0=%X\n",__func__, readl(pmu_base +PMU_PWR_UP_DELAY_0) );

 out:
	of_node_put(np);
	return ret;
}

static void armada_370_xp_pmsu_enable_l2_powerdown_onidle(void)
{
	u32 reg;

	if (pmsu_mp_base == NULL)
		return;

	/* Enable L2 & Fabric powerdown in Deep-Idle mode - Fabric */
	reg = readl(pmsu_mp_base + L2C_NFABRIC_PM_CTL);
	reg |= L2C_NFABRIC_PM_CTL_PWR_DOWN;
	writel(reg, pmsu_mp_base + L2C_NFABRIC_PM_CTL);
}

static void armada_370_xp_cpu_resume(void)
{
	asm volatile("bl    ll_add_cpu_to_smp_group\n\t"
		     "bl    ll_enable_coherency\n\t"
		     "b	    cpu_resume\n\t");
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
	if (of_machine_is_compatible("marvell,armadaxp")) {
		reg = readl(pmsu_mp_base + PMSU_CPU_POWER_DOWN_CONTROL(hw_cpu));
		reg |= PMSU_CPU_POWER_DOWN_DIS_SNP_Q_SKIP;
		writel(reg, pmsu_mp_base + PMSU_CPU_POWER_DOWN_CONTROL(hw_cpu));

	}

}

static noinline int do_armada_370_xp_cpu_suspend(unsigned long deepidle)
{
	armada_370_xp_pmsu_idle_prepare(deepidle);

	v7_exit_coherency_flush(all);

	ll_disable_coherency();

	dsb();

	wfi();

	/* If we are here, wfi failed. As processors run out of
	 * coherency for some time, tlbs might be stale, so flush them
	 */
	local_flush_tlb_all();

	ll_enable_coherency();

	/* Test the CR_C bit and set it if it was cleared */
	asm volatile(
	"mrc	p15, 0, %0, c1, c0, 0 \n\t"
	"tst	%0, #(1 << 2) \n\t"
	"orreq	%0, %0, #(1 << 2) \n\t"
	"mcreq	p15, 0, %0, c1, c0, 0 \n\t"
	"isb	"
	: : "r" (0));

	pr_warn("Failed to suspend the system\n");

	return 0;
}

static int armada_370_xp_cpu_suspend(unsigned long deepidle)
{
	return cpu_suspend(deepidle, do_armada_370_xp_cpu_suspend);
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


	local_flush_tlb_all();

	ll_enable_coherency();

	/* cancel Enable wakeup events and mask interrupts */
	reg = readl(pmsu_mp_base + PMSU_STATUS_AND_MASK(hw_cpu));
	reg &= ~(PMSU_STATUS_AND_MASK_IRQ_WAKEUP | PMSU_STATUS_AND_MASK_FIQ_WAKEUP);
	reg &= ~PMSU_STATUS_AND_MASK_CPU_IDLE_WAIT;
	reg &= ~PMSU_STATUS_AND_MASK_SNP_Q_EMPTY_WAIT;
	reg &= ~(PMSU_STATUS_AND_MASK_IRQ_MASK | PMSU_STATUS_AND_MASK_FIQ_MASK);
	writel(reg, pmsu_mp_base + PMSU_STATUS_AND_MASK(hw_cpu));
}

static int armada_370_xp_cpu_pm_notify(struct notifier_block *self,
				    unsigned long action, void *hcpu)
{
	if (action == CPU_PM_ENTER) {
		unsigned int hw_cpu = cpu_logical_map(smp_processor_id());
		mvebu_pmsu_set_cpu_boot_addr(hw_cpu, armada_370_xp_cpu_resume);
	} else if (action == CPU_PM_EXIT) {
		armada_370_xp_pmsu_idle_restore();
	}

	return NOTIFY_OK;
}

static struct notifier_block armada_370_xp_cpu_pm_notifier = {
	.notifier_call = armada_370_xp_cpu_pm_notify,
};

int __init armada_370_xp_cpu_pm_init(void)
{
	struct device_node *np;

	/*
	 * Check that all the requirements are available to enable
	 * cpuidle. So far, it is only supported on Armada XP, cpuidle
	 * needs the coherency fabric and the PMSU enabled
	 */
/*
	if (!of_machine_is_compatible("marvell,armadaxp"))
		return 0;
*/

	np = of_find_compatible_node(NULL, NULL, "marvell,coherency-fabric");
	if (!np)
		return 0;
	of_node_put(np);

	np = of_find_matching_node(NULL, of_pmsu_table);
	if (!np)
		return 0;
	of_node_put(np);

	a370_cpuidle_enable_wa();

	armada_370_xp_pmsu_enable_l2_powerdown_onidle();
	armada_xp_cpuidle_device.dev.platform_data = armada_370_xp_cpu_suspend;
	platform_device_register(&armada_xp_cpuidle_device);
	cpu_pm_register_notifier(&armada_370_xp_cpu_pm_notifier);

	return 0;
}

arch_initcall(armada_370_xp_cpu_pm_init);
early_initcall(armada_370_xp_pmsu_init);
