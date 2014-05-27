/*
 * Suspend/resume
 *
 * Copyright (C) 2014 Marvell
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/cpu_pm.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/suspend.h>
#include <linux/of_address.h>
#include <asm/cacheflush.h>
#include <asm/outercache.h>
#include <asm/suspend.h>

#include "coherency.h"

#define SDRAM_CONFIG_OFFS                  0x0
#define  SDRAM_CONFIG_SR_MODE_BIT          BIT(24)
#define SDRAM_OPERATION_OFFS               0x18
#define  SDRAM_OPERATION_SELF_REFRESH      0x7
#define SDRAM_DLB_EVICTION_OFFS            0x30c
#define  SDRAM_DLB_EVICTION_THRESHOLD_MASK 0xff

static int mvebu_pm_powerdown(unsigned long data)
{
	void __iomem *sdram_ctrl = ioremap(0xf1001400, 0x500);
	void __iomem *gpio_ctrl = ioremap(0xf1018100, 4);
	u32 reg, srcmd, ackcmd;

	/* 1. flush L1 dcache */
	flush_cache_all();
	/* 2. flush L2 */
	outer_flush_all();

	/*
	 * Issue a Data Synchronization Barrier instruction to ensure
	 * that all state saving has been completed.
	 */
	dsb();

	/* Flush the DLB and wait ~7 usec */
	/* Clear bits 7:0 in 'DLB Eviction Control Register', 0x170C */
	reg = readl(sdram_ctrl + SDRAM_DLB_EVICTION_OFFS);
	reg &= ~SDRAM_DLB_EVICTION_THRESHOLD_MASK;
	writel(reg, sdram_ctrl + SDRAM_DLB_EVICTION_OFFS);

	udelay(7);

	/* Set DRAM in battery backup mode */
	reg = readl(sdram_ctrl + SDRAM_CONFIG_OFFS);
	reg &= ~SDRAM_CONFIG_SR_MODE_BIT;
	writel(reg, sdram_ctrl + SDRAM_CONFIG_OFFS);

	/* Prepare to go to self-refresh */
	/* Involves writing 0x7 to 'SDRAM Operation Register', 0x1418 */

	/* Configure GPIOs 18:17:16 as output */
	gpio_request(16, "pic-pin0");
	gpio_request(17, "pic-pin1");
	gpio_request(18, "pic-pin2");
	gpio_direction_output(16, 0);
	gpio_direction_output(17, 0);
	gpio_direction_output(18, 0);

	/* Put 001 as value on GPIOs 18:17:16 */
	reg = readl(gpio_ctrl);
	reg &= ~(BIT(16) | BIT(17) | BIT(18));
	reg |= BIT(16);
	writel(reg, gpio_ctrl);

	srcmd = readl(sdram_ctrl + SDRAM_OPERATION_OFFS);
	srcmd &= ~0x1F;
	srcmd |= SDRAM_OPERATION_SELF_REFRESH;

	ackcmd = readl(gpio_ctrl);
	ackcmd |= BIT(16) | BIT(17) | BIT(18);

	/* Prepare writing 111 to 18:17:16 */

	/* Wait a while */
	mdelay(250);

	/* Enter self-refresh */
	/* Wait 100 cycles */
	/* Send the 111 to 18:17:16 */
	/* Trap the processor */

	asm volatile (
		/* Align to a cache line */
		".balign 32\n\t"

		/* Enter self refresh */
		"str %[srcmd], [%[sdram_ctrl], #" __stringify(SDRAM_OPERATION_OFFS) "]\n\t"

		/* Wait 100 cycles for DDR to enter self refresh */
		"1: subs r1, r1, #1\n\t"
		"bne 1b\n\t"

		/* Issue the command ACK */
		"str %[ackcmd], [%[gpio_ctrl]]\n\t"

		/* Trap the processor */
		"b .\n\t"
		: : [srcmd] "r" (srcmd), [sdram_ctrl] "r" (sdram_ctrl),
		  [ackcmd] "r" (ackcmd), [gpio_ctrl] "r" (gpio_ctrl) : "r1");

	/* Never reached */
	return 0;
}

#define BOOT_INFO_ADDR      0x3000
#define BOOT_MAGIC_WORD	    0xdeadb002
#define BOOT_MAGIC_LIST_END 0xffffffff

/*
 * Those registers are accessed before switching the internal register
 * base, which is why we hardcode the 0xd0000000 base address, the one
 * used by the SoC out of reset.
 */
#define MBUS_WINDOW_12_CTRL       0xd00200b0
#define MBUS_INTERNAL_REG_ADDRESS 0xd0020080

#define SDRAM_WIN_BASE_REG(x)	(0x20180 + (0x8*x))
#define SDRAM_WIN_CTRL_REG(x)	(0x20184 + (0x8*x))

extern void armada_370_xp_cpu_resume(void);

static phys_addr_t mvebu_internal_reg_base(void)
{
	struct device_node *np;
	__be32 in_addr[2];

	np = of_find_node_by_name(NULL, "internal-regs");
	BUG_ON(!np);

	in_addr[0] = cpu_to_be32(0xf0010000);
	in_addr[1] = 0x0;

	return of_translate_address(np, in_addr);
}

static void mvebu_pm_store_bootinfo(void)
{
	u32 *store_addr;
	void *sdram = ioremap(0xf1020180, 4096);
	phys_addr_t resume_pc;
	u32 reg;
	int i;

	store_addr = phys_to_virt(BOOT_INFO_ADDR);
	resume_pc = virt_to_phys(armada_370_xp_cpu_resume);

	/*
	 * The bootloader expects the first two words to be a magic
	 * value (BOOT_MAGIC_WORD), followed by the address of the
	 * resume code to jump to. Then, it expects a sequence of
	 * (address, value) pairs, which can be used to restore the
	 * value of certain registers. This sequence must end with the
	 * BOOT_MAGIC_LIST_END magic value.
	 */

	writel(BOOT_MAGIC_WORD, store_addr++);
	writel(resume_pc, store_addr++);

	/*
	 * Some platforms remap their internal register base address
	 * to 0xf1000000. However, out of reset, window 12 starts at
	 * 0xf0000000 and ends at 0xf7ffffff, which would overlap with
	 * the internal registers. Therefore, disable window 12.
	 */
	writel(MBUS_WINDOW_12_CTRL, store_addr++);
	writel(0x0, store_addr++);

	/*
	 * Set the internal register base address to the value
	 * expected by Linux, as read from the Device Tree.
	 */
	writel(MBUS_INTERNAL_REG_ADDRESS, store_addr++);
	writel(mvebu_internal_reg_base(), store_addr++);

	for (i = 0; i < 4; i++) {
		writel(0xf1000000 + SDRAM_WIN_BASE_REG(i), store_addr++);
		reg = readl(sdram + 0x8 * i);
		writel(reg, store_addr++);

		writel(0xf1000000 + SDRAM_WIN_CTRL_REG(i), store_addr++);
		reg = readl(sdram + 0x4 + 0x8 * i);
		writel(reg, store_addr++);
	}

	writel(BOOT_MAGIC_LIST_END, store_addr);
}

extern void mvebu_v7_pmsu_idle_exit(void);

static int mvebu_pm_enter(suspend_state_t state)
{
	if (state != PM_SUSPEND_MEM)
		return -EINVAL;

	cpu_pm_enter();
	cpu_cluster_pm_enter();

	mvebu_pm_store_bootinfo();
	cpu_suspend(0, mvebu_pm_powerdown);

	pr_info("In %s, returning from suspend\n", __func__);

	outer_resume();

	mvebu_v7_pmsu_idle_exit();

	set_cpu_coherent();

	cpu_cluster_pm_exit();
	cpu_pm_exit();

	return 0;
}

static const struct platform_suspend_ops mvebu_pm_ops = {
        .enter = mvebu_pm_enter,
        .valid = suspend_valid_only_mem,
};

static int mvebu_pm_init(void)
{
	suspend_set_ops(&mvebu_pm_ops);
	return 0;
}

arch_initcall(mvebu_pm_init);
