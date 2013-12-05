/*
 * USB cluster support for Armada 375 platform.
 *
 * Copyright (C) 2014 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2 or later. This program is licensed "as is"
 * without any warranty of any kind, whether express or implied.
 *
 * Armada 375 comes with an USB2 host and device controller and an
 * USB3 controller. The USB cluster control register allows to manage
 * common features of both USB controllers.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define USB2_PHY_CONFIG_DISABLE BIT(0)

/* The USB cluster allows to choose between two PHYs */
#define NB_PHY 2

enum {
	PHY_USB2 = 0,
	PHY_USB3 = 1,
};

struct armada375_cluster_phy {
	struct phy *phy;
	void __iomem *reg;
	bool enable;
	bool use_usb3;
};

struct armada375_cluster_phy usb_cluster_phy[NB_PHY];

static int armada375_usb_phy_init(struct phy *phy)
{
	struct armada375_cluster_phy *cluster_phy = phy_get_drvdata(phy);
	u32 reg;

	if (!cluster_phy->enable)
		return -ENODEV;

	reg = readl(cluster_phy->reg);
	if (cluster_phy->use_usb3)
		reg |= USB2_PHY_CONFIG_DISABLE;
	else
		reg &= ~USB2_PHY_CONFIG_DISABLE;
	writel(reg, cluster_phy->reg);

	return 0;
}

static struct phy_ops armada375_usb_phy_ops = {
	.init = armada375_usb_phy_init,
	.owner		= THIS_MODULE,
};

static struct phy *armada375_usb_phy_xlate(struct device *dev,
					struct of_phandle_args *args)
{
	if (WARN_ON(args->args[0] >= NB_PHY))
		return ERR_PTR(-ENODEV);

	return usb_cluster_phy[args->args[0]].phy;
}

static int armada375_usb_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy *phy;
	struct phy_provider *phy_provider;
	void __iomem *usb_cluster_base;
	struct device_node *xhci_node;
	struct resource *res;
	int i;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	usb_cluster_base = devm_ioremap_resource(&pdev->dev, res);
	if (!usb_cluster_base)
		return -ENOMEM;

	for (i = 0; i < NB_PHY; i++) {
		phy = devm_phy_create(dev, &armada375_usb_phy_ops, NULL);
		if (IS_ERR(phy)) {
			dev_err(dev, "failed to create PHY n%d\n", i);
			return PTR_ERR(phy);
		}

		usb_cluster_phy[i].phy = phy;
		usb_cluster_phy[i].reg = usb_cluster_base;
		usb_cluster_phy[i].enable = false;
		phy_set_drvdata(phy, &usb_cluster_phy[i]);
	}

	usb_cluster_phy[PHY_USB2].use_usb3 = false;
	usb_cluster_phy[PHY_USB3].use_usb3 = true;

	/*
	 * We can't use the first usb2 unit and usb3 at the same time
	 * to manage a USB2 device, so let's disable usb2 if usb3 is
	 * selected. In this case the USB2 device will be managed by
	 * the xhci controller.
	 */

	xhci_node = of_find_compatible_node(NULL, NULL,
					"marvell,armada-375-xhci");

	if (xhci_node && of_device_is_available(xhci_node)) {
		usb_cluster_phy[PHY_USB3].enable = true;
	} else {
		struct device_node *ehci_node;
		ehci_node = of_find_compatible_node(NULL, NULL,
					"marvell,orion-ehci");
		if (ehci_node && of_device_is_available(ehci_node))
			usb_cluster_phy[PHY_USB2].enable = true;
		of_node_put(ehci_node);
	}

	of_node_put(xhci_node);

	phy_provider = devm_of_phy_provider_register(&pdev->dev,
						     armada375_usb_phy_xlate);
	if (IS_ERR(phy_provider))
		return PTR_ERR(phy_provider);

	return 0;
}

static const struct of_device_id of_usb_cluster_table[] = {
	{ .compatible = "marvell,armada-375-usb-cluster", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, of_usb_cluster_table);

static struct platform_driver armada375_usb_phy_driver = {
	.probe	= armada375_usb_phy_probe,
	.driver = {
		.of_match_table	= of_usb_cluster_table,
		.name  = "armada-375-usb-cluster",
		.owner = THIS_MODULE,
	}
};
module_platform_driver(armada375_usb_phy_driver);

MODULE_DESCRIPTION("Armada 375 USB cluster driver");
MODULE_AUTHOR("Gregory CLEMENT <gregory.clement@free-electrons.com>");
MODULE_LICENSE("GPL");
