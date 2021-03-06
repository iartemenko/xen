/*
 *  Copyright (C) 2016,2017 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This device provides a mechanism for emulating a mailbox by using
 * smc calls, allowing a "mailbox" consumer to sit in firmware running
 * on the same core.
 *
 * Based on patch series which hasn't reach upstream yet:
 * => https://lkml.org/lkml/2017/7/23/129
 *    [PATCH v2 0/3] mailbox: arm: introduce smc triggered mailbox
 *
 * Xen modification:
 * Oleksandr Tyshchenko <Oleksandr_Tyshchenko@epam.com>
 * Copyright (C) 2017 EPAM Systems Inc.
 */

#if 0
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/arm-smccc.h>
#endif

#include <xen/device_tree.h>
#include <xen/err.h>
#include <xen/xmalloc.h>

#include "mailbox_controller.h"
#include "wrappers.h"

/*
 * TODO:
 * 1. Add releasing resources since devm.
 */

struct arm_smccc_res {
	unsigned long a0;
	unsigned long a1;
	unsigned long a2;
	unsigned long a3;
};

/* This is just to align interfaces. */
static inline void arm_smccc_smc(unsigned long a0, unsigned long a1,
		unsigned long a2, unsigned long a3, unsigned long a4,
		unsigned long a5, unsigned long a6, unsigned long a7,
		struct arm_smccc_res *res)
{
	register_t ret[4] = { 0 };

	call_smccc_smc(a0, a1, a2, a3, a4, a5, a6, a7, ret);

	res->a0 = ret[0];
	res->a1 = ret[1];
	res->a2 = ret[2];
	res->a3 = ret[3];
}

static inline void arm_smccc_hvc(unsigned long a0, unsigned long a1,
		unsigned long a2, unsigned long a3, unsigned long a4,
		unsigned long a5, unsigned long a6, unsigned long a7,
		struct arm_smccc_res *res)
{
	/*
	 * We should never get here since the "use_hvc" flag is always false
	 * (smc is only allowed method).
	*/
	BUG();
}

/***** Start of Linux code *****/

#define ARM_SMC_MBOX_USE_HVC	BIT(0)

struct arm_smc_chan_data {
	u32 function_id;
	u32 flags;
};

static int arm_smc_send_data(struct mbox_chan *link, void *data)
{
	struct arm_smc_chan_data *chan_data = link->con_priv;
	u32 function_id = chan_data->function_id;
	struct arm_smccc_res res;
	u32 msg = *(u32 *)data;

	if (chan_data->flags & ARM_SMC_MBOX_USE_HVC)
		arm_smccc_hvc(function_id, msg, 0, 0, 0, 0, 0, 0, &res);
	else
		arm_smccc_smc(function_id, msg, 0, 0, 0, 0, 0, 0, &res);

	mbox_chan_received_data(link, (void *)res.a0);

	return 0;
}

static const struct mbox_chan_ops arm_smc_mbox_chan_ops = {
	.send_data	= arm_smc_send_data,
};

static int arm_smc_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mbox_controller *mbox;
	struct arm_smc_chan_data *chan_data;
	const char *method;
	bool use_hvc = false;
	int ret, i;

	ret = of_property_count_elems_of_size(dev->of_node, "arm,func-ids",
					      sizeof(u32));
	if (ret < 0)
		return ret;

	if (!of_property_read_string(dev->of_node, "method", &method)) {
		if (!strcmp("hvc", method)) {
			use_hvc = true;

			dev_warn(dev,"method must be smc\n");
			return -EINVAL;
		} else if (!strcmp("smc", method)) {
			use_hvc = false;
		} else {
			dev_warn(dev, "invalid \"method\" property: %s\n",
				 method);

			return -EINVAL;
		}
	}

	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	mbox->num_chans = ret;
	mbox->chans = devm_kcalloc(dev, mbox->num_chans, sizeof(*mbox->chans),
				   GFP_KERNEL);
	if (!mbox->chans)
		return -ENOMEM;

	chan_data = devm_kcalloc(dev, mbox->num_chans, sizeof(*chan_data),
				 GFP_KERNEL);
	if (!chan_data)
		return -ENOMEM;

	for (i = 0; i < mbox->num_chans; i++) {
		u32 function_id;

		ret = of_property_read_u32_index(dev->of_node,
						 "arm,func-ids", i,
						 &function_id);
		if (ret)
			return ret;

		chan_data[i].function_id = function_id;
		if (use_hvc)
			chan_data[i].flags |= ARM_SMC_MBOX_USE_HVC;
		mbox->chans[i].con_priv = &chan_data[i];
	}

	mbox->txdone_poll = false;
	mbox->txdone_irq = false;
	/*
	 * We don't have RX-done irq, but always have received data in hand since
	 * mailbox is synchronous.
	 */
	mbox->rxdone_auto = true;
	mbox->ops = &arm_smc_mbox_chan_ops;
	mbox->dev = dev;

	ret = mbox_controller_register(mbox);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, mbox);
	dev_info(dev, "ARM SMC mailbox enabled with %d chan%s.\n",
		 mbox->num_chans, mbox->num_chans == 1 ? "" : "s");

	return ret;
}

#if 0
static int arm_smc_mbox_remove(struct platform_device *pdev)
{
	struct mbox_controller *mbox = platform_get_drvdata(pdev);

	mbox_controller_unregister(mbox);
	return 0;
}
#endif

static const struct of_device_id arm_smc_mbox_of_match[] = {
	{ .compatible = "arm,smc-mbox", },
	{},
};
MODULE_DEVICE_TABLE(of, arm_smc_mbox_of_match);

#if 0
static struct platform_driver arm_smc_mbox_driver = {
	.driver = {
		.name = "arm-smc-mbox",
		.of_match_table = arm_smc_mbox_of_match,
	},
	.probe		= arm_smc_mbox_probe,
	.remove		= arm_smc_mbox_remove,
};
module_platform_driver(arm_smc_mbox_driver);

MODULE_AUTHOR("Andre Przywara <andre.przywara@arm.com>");
MODULE_DESCRIPTION("Generic ARM smc mailbox driver");
MODULE_LICENSE("GPL v2");
#endif

/***** End of Linux code *****/

static int __init arm_smc_mbox_init(struct dt_device_node *dev,
		const void *data)
{
	int ret;

	ret = arm_smc_mbox_probe(dev);
	if (ret) {
		dev_err(&dev->dev, "failed to init ARM SMC mailbox (%d)\n", ret);
		return ret;
	}

	return 0;
}

DT_DEVICE_START(arm_smc_mbox, "ARM SMC mailbox", DEVICE_MAILBOX)
	.dt_match = arm_smc_mbox_of_match,
	.init = arm_smc_mbox_init,
DT_DEVICE_END

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
