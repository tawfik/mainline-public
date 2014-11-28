/*
 * RTC driver for the Armada 38x Marvell SoCs
 *
 * Copyright (C) 2014 Marvell
 *
 * Gregory Clement <gregory.clement@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 */
#define DEBUG 0xff

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

#define RTC_STATUS	0x0
#define RTC_STATUS_ALARM1	BIT(0)
#define RTC_STATUS_ALARM2	BIT(1)
#define RTC_IRQ1_CONF	0x4
#define RTC_IRQ1_AL_EN		BIT(0)
#define RTC_IRQ1_FREQ_EN	BIT(1)
#define RTC_IRQ2_CONF	0x8
#define RTC_TIME	0xC
#define RTC_ALARM1	0x10
#define RTC_ALARM2	0x14
#define RTC_CLOCK_CORR	0x18
#define RTC_TEST	0x1C

#define SOC_RTC_INTERRUPT   0x8
#define SOC_RTC_ALARM1		BIT(0)
#define SOC_RTC_ALARM2		BIT(1)
#define SOC_RTC_ALARM1_MASK	BIT(2)
#define SOC_RTC_ALARM2_MASK	BIT(3)

struct armada38x_rtc {
	struct rtc_device   *rtc_dev;
	void __iomem	    *regs;
	void __iomem	    *regs_soc;
	spinlock_t	    lock;
	int		    irq;
};

static int armada38x_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct armada38x_rtc *rtc = dev_get_drvdata(dev);
	unsigned long time, time_check, flags;

	spin_lock_irqsave(&rtc->lock, flags);
	time = readl(rtc->regs + RTC_TIME);
	/* WA for failing time set attempts. The HW ERRATA information
	 * should be added here if detected more than one second
	 * between two time reads, read once again */
	time_check = readl(rtc->regs + RTC_TIME);
	if ((time_check - time) > 1)
		time_check = readl(rtc->regs + RTC_TIME);

	spin_unlock_irqrestore(&rtc->lock, flags);

	rtc_time_to_tm(time_check, tm);

	return 0;
}

static int armada38x_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct armada38x_rtc *rtc = dev_get_drvdata(dev);
	int ret = 0;
	unsigned long time,  flags;

	ret = rtc_tm_to_time(tm, &time);

	if (ret)
		goto out;

	spin_lock_irqsave(&rtc->lock, flags);
	/* WA for failing time set attempts. The HW ERRATA information
	 * should be added here */
	writel(0, rtc->regs + RTC_STATUS);
	mdelay(1000);

	writel(time, rtc->regs + RTC_TIME);
	spin_unlock_irqrestore(&rtc->lock, flags);

out:
	return ret;
}

static int armada38x_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct armada38x_rtc *rtc = dev_get_drvdata(dev);
	unsigned long time, flags;
	u32 val;

	spin_lock_irqsave(&rtc->lock, flags);

	time = readl(rtc->regs + RTC_ALARM1);
	val = readl(rtc->regs + RTC_IRQ1_CONF) & RTC_IRQ1_AL_EN;

	spin_unlock_irqrestore(&rtc->lock, flags);

	alrm->enabled = val ? 1 : 0;
	rtc_time_to_tm(time,  &alrm->time);

	return 0;
}

static int armada38x_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct armada38x_rtc *rtc = dev_get_drvdata(dev);
	unsigned long time, flags;
	int ret = 0;
	u32 val;


	ret = rtc_tm_to_time(&alrm->time, &time);

	if (ret)
		goto out;

	spin_lock_irqsave(&rtc->lock, flags);

	writel(time, rtc->regs + RTC_ALARM1);

	if (alrm->enabled) {
		udelay(5);
		writel(RTC_IRQ1_AL_EN ,
			rtc->regs + RTC_IRQ1_CONF);
		udelay(5);
		val = readl(rtc->regs_soc + SOC_RTC_INTERRUPT);
		writel(val | SOC_RTC_ALARM1_MASK,
			rtc->regs_soc + SOC_RTC_INTERRUPT);
	}

	spin_unlock_irqrestore(&rtc->lock, flags);
out:
	return ret;
}

static int armada38x_rtc_alarm_irq_enable(struct device *dev,
					 unsigned int enabled)
{
	struct armada38x_rtc *rtc = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&rtc->lock, flags);

	if (enabled)
		writel(RTC_IRQ1_AL_EN, rtc->regs + RTC_IRQ1_CONF);
	else
		writel(0, rtc->regs + RTC_IRQ1_CONF);

	spin_unlock_irqrestore(&rtc->lock, flags);

	return 0;
}

static irqreturn_t armada38x_rtc_alarm_irq(int irq, void *data)
{
	struct armada38x_rtc *rtc = data;
	u32 val;
	unsigned long flags;

	dev_dbg(&rtc->rtc_dev->dev, "%s:irq(%d)\n", __func__, irq);

	val = readl(rtc->regs_soc + SOC_RTC_INTERRUPT);
	spin_lock_irqsave(&rtc->lock, flags);

	/* disable all the interrupts for alarm 1 */
	writel(0, rtc->regs + RTC_IRQ1_CONF);
	/* Ack the event */
	writel(RTC_STATUS_ALARM1, rtc->regs + RTC_STATUS);
	spin_unlock_irqrestore(&rtc->lock, flags);

	rtc_update_irq(rtc->rtc_dev, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops armada38x_rtc_ops = {
	.read_time = armada38x_rtc_read_time,
	.set_time = armada38x_rtc_set_time,
	.read_alarm = armada38x_rtc_read_alarm,
	.set_alarm = armada38x_rtc_set_alarm,
	.alarm_irq_enable = armada38x_rtc_alarm_irq_enable,
};

static int armada38x_rtc_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct armada38x_rtc *rtc;
	int ret;

	dev_dbg(&pdev->dev, "%s\n", __func__);
	pr_err("%s\n", __func__);

	rtc = devm_kzalloc(&pdev->dev, sizeof(struct armada38x_rtc),
			    GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	spin_lock_init(&rtc->lock);
pr_err("%s_%d\n",__func__,__LINE__);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rtc->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rtc->regs))
		return PTR_ERR(rtc->regs);
pr_err("%s_%d\n",__func__,__LINE__);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	rtc->regs_soc = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rtc->regs_soc))
		return PTR_ERR(rtc->regs_soc);
pr_err("%s_%d\n",__func__,__LINE__);
	rtc->irq = platform_get_irq(pdev, 0);

	if (rtc->irq < 0) {
		dev_err(&pdev->dev, "no irq\n");
		return rtc->irq;
	}
pr_err("%s_%d\n",__func__,__LINE__);
	if (devm_request_irq(&pdev->dev, rtc->irq, armada38x_rtc_alarm_irq,
				0, pdev->name, rtc) < 0) {
		dev_warn(&pdev->dev, "interrupt not available.\n");
		rtc->irq = -1;
	}
pr_err("%s_%d\n",__func__,__LINE__);
	platform_set_drvdata(pdev, rtc);

	device_init_wakeup(&pdev->dev, 1);
pr_err("%s_%d\n",__func__,__LINE__);
	rtc->rtc_dev = devm_rtc_device_register(&pdev->dev, pdev->name,
					&armada38x_rtc_ops, THIS_MODULE);
pr_err("%s_%d\n",__func__,__LINE__);
	if (IS_ERR(rtc->rtc_dev)) {
		ret = PTR_ERR(rtc->rtc_dev);
		dev_err(&pdev->dev, "Failed to register RTC device: %d\n", ret);
		if (ret == 0)
			ret = -EINVAL;
		return ret;
	}
pr_err("%s_%d\n",__func__,__LINE__);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int armada38x_rtc_suspend(struct device *dev)
{
	if (device_may_wakeup(dev)) {
		struct armada38x_rtc *rtc = dev_get_drvdata(dev);

		return enable_irq_wake(rtc->irq);
	}

	return 0;
}

static int armada38x_rtc_resume(struct device *dev)
{
	if (device_may_wakeup(dev)) {
		struct armada38x_rtc *rtc = dev_get_drvdata(dev);

		return disable_irq_wake(rtc->irq);
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(armada38x_rtc_pm_ops,
			 armada38x_rtc_suspend, armada38x_rtc_resume);

#ifdef CONFIG_OF
static const struct of_device_id armada38x_rtc_of_match_table[] = {
	{ .compatible = "marvell,armada-380-rtc", },
	{}
};
#endif

static struct platform_driver armada38x_rtc_driver = {
	.driver		= {
		.name	= "armada38x-rtc",
		.pm	= &armada38x_rtc_pm_ops,
		.of_match_table = of_match_ptr(armada38x_rtc_of_match_table),
	},
};

module_platform_driver_probe(armada38x_rtc_driver, armada38x_rtc_probe);

MODULE_DESCRIPTION("Marvell Armada 38x RTC driver");
MODULE_AUTHOR("Gregory CLEMENT <gregory.clement@free-electrons.com>");
MODULE_LICENSE("GPL");
