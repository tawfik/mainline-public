#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/mbus.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define MG_UNIT_ID         0x0b
#define MG_UNIT_ATTR       0x4
#define MG_UNIT_CPU_ADDR   0xf8000000
#define MG_UNIT_SZ         0x400000
#define MG_UNIT_REMAP_ADDR 0x0

/* WARNING: all error handling omitted */
static int mvpp3_probe(struct platform_device *pdev)
{
	struct resource *a2m0, *a2m1, *gic, *nss_regs, *nss_space;
	struct clk *clk;

	a2m0 = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	a2m1 = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	gic = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	nss_regs = platform_get_resource(pdev, IORESOURCE_MEM, 3);
	nss_space = platform_get_resource(pdev, IORESOURCE_MEM, 4);

	dev_info(&pdev->dev, "A2M Master 0: 0x%x", a2m0->start);
	dev_info(&pdev->dev, "A2M Master 1: 0x%x", a2m1->start);
	dev_info(&pdev->dev, "GIC: 0x%x", gic->start);
	dev_info(&pdev->dev, "NSS registers: 0x%x", nss_regs->start);
	dev_info(&pdev->dev, "NSS space: 0x%x", nss_space->start);

	/*
	 * The mvebu-mbus DT binding currently doesn't allow
	 * describing static windows with the remap capability, so we
	 * simply use the mvebu-mbus API to dynamically create the
	 * required window. This should be changed once mvebu-mbus is
	 * extended to cover such a case.
	 */
	mvebu_mbus_add_window_remap_by_id(MG_UNIT_ID, MG_UNIT_ATTR,
					  MG_UNIT_CPU_ADDR, MG_UNIT_SZ,
					  MG_UNIT_REMAP_ADDR);

	clk = devm_clk_get(&pdev->dev, NULL);
	clk_prepare_enable(clk);

	dev_info(&pdev->dev, "Clock rate: %lu", clk_get_rate(clk));

	return 0;
}

static int mvpp3_remove(struct platform_device *pdev)
{
	mvebu_mbus_del_window(MG_UNIT_CPU_ADDR, MG_UNIT_SZ);
	return 0;
}

static const struct of_device_id mvpp3_match[] = {
	{ .compatible = "marvell,armada-390-pp3" },
	{ }
};
MODULE_DEVICE_TABLE(of, mvpp3_match);

static struct platform_driver mvpp3_driver = {
	.probe = mvpp3_probe,
	.remove = mvpp3_remove,
	.driver = {
		.name = "mvpp3",
		.of_match_table = mvpp3_match,
	},
};

module_platform_driver(mvpp3_driver);
