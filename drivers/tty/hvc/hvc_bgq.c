/*
 * Blue Gene/Q Platform
 * authors:
 *    Andrew Tauferner <ataufer@us.ibm.com>
 *    Todd Inglett <tinglett@us.ibm.com>
 *    Jimi Xenidis <jimix@pobox.com>
 *    Eric Van Hensbergen <ericvh@gmail.com>
 *
 * Licensed Materials - Property of IBM
 *
 * Blue Gene/Q
 *
 * (c) Copyright IBM Corp. 2011, 2012 All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM
 * Corporation.
 *
 * This software is available to you under the GNU General Public
 * License (GPL) version 2.
 */

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/console.h>

#include <asm/dcr.h>

#include <platforms/bgq/bgq.h>

#include "hvc_console.h"


static const struct hv_ops hvc_bgq_ops = {
	.get_chars = bgq_inbox_poll,
	.put_chars = bgq_put_chars,
	.notifier_add = notifier_add_irq,
	.notifier_del = notifier_del_irq,
	.notifier_hangup = notifier_hangup_irq,
};

static int __devinit hvc_bgq_probe(struct platform_device *pdev)
{
	struct device_node *dn;
	struct hvc_struct *hp;
	unsigned irq;

	dn = pdev->dev.of_node;
	BUG_ON(!dn);

	hvc_instantiate(0, 0, &hvc_bgq_ops);

	irq = irq_of_parse_and_map(dn, 0);
	if (irq == IRQ_TYPE_NONE) {
		pr_warn("%s: continuing with no IRQ\n", __func__);
		irq = 0;
	}

	hp = hvc_alloc(0, irq, &hvc_bgq_ops, 256);
	if (IS_ERR(hp))
		return PTR_ERR(hp);

	pr_info("hvc%u: console mapped to: %s\n", hp->vtermno, dn->full_name);
	add_preferred_console("hvc", 0, NULL);
	dev_set_drvdata(&pdev->dev, hp);

	return 0;
}

static int __devexit hvc_bgq_remove(struct platform_device *dev)
{
	struct hvc_struct *hp = dev_get_drvdata(&dev->dev);
	return hvc_remove(hp);
}

static const struct of_device_id hvc_bgq_device_id[] = {
	{ .compatible	= "ibm,bgq-mailbox" },
	{}
};

static struct platform_driver hvc_bgq_driver = {
	.probe		= hvc_bgq_probe,
	.remove		= __devexit_p(hvc_bgq_remove),
	.driver		= {
		.name	= "hvc-bgq-console",
		.owner	= THIS_MODULE,
		.of_match_table = hvc_bgq_device_id,
	},
};

static int __init hvc_bgq_init(void)
{
	return platform_driver_register(&hvc_bgq_driver);
}

module_init(hvc_bgq_init);

static void __exit hvc_bgq_exit(void)
{
	platform_driver_unregister(&hvc_bgq_driver);
}
module_exit(hvc_bgq_exit);
