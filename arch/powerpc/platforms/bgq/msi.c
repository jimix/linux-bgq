/*
 * Blue Gene/Q Platform
 * authors:
 *    Andrew Tauferner <ataufer@us.ibm.com>
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

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/msi.h>
#include <asm/msi_bitmap.h>
#include <linux/of_platform.h>
#include <asm/udbg.h>

#include "bgq.h"
#include "bic.h"

struct bgq_msi {
	void __iomem *msi_regs;
	/* should this be a pointer */
	struct msi_bitmap bitmap;
	struct irq_domain *irq_host;
	dcr_host_t dcr_devbus;
	unsigned bitmap_bits;
};

static struct bgq_msi bgq_msi;

int bgq_msi_get_irq(u8 msi_reg)
{
	int irq = NO_IRQ;
	u8 msi_word_count = 0;
	void __iomem *reg;
	void __iomem *ack = bgq_msi.msi_regs + (msi_reg * 64UL);

	do {
		u64 val;
		unsigned fb;

		reg = ack + 0x100; /* ack + 0x100 bytes */

		val = in_be64(reg);
		fb = find_first_bit((ulong *)&val, 64);

		if (fb < 64) {
			u16 vector = (63 - fb) + (64 * msi_word_count);

			/*
			 * Clear the MSI vector bit and map to the
			 * virtual IRQ number. The MSI register to
			 * clear a vector bit is 0x100 bytes less than
			 * the MSI register from which we read.
			 */
			out_be64(ack, (1ULL << fb));
			irq = irq_radix_revmap_lookup(bgq_msi.irq_host,
						      vector);
			break;
		}

		msi_word_count++;
		/* The 64b words of the 265b MSI register are 16B aligned. */
		ack += 16;
	} while (irq == NO_IRQ && msi_word_count < 4);

	return irq;
}

static unsigned int bgq_msi_startup(struct irq_data *d)
{
	unmask_msi_irq(d);
	return 0;
}

static void bgq_msi_ack_irq(struct irq_data *d)
{
}

static struct irq_chip bgq_msi_irq_chip = {
	.name = "Blue Gene/Q MSI",
	.irq_startup = bgq_msi_startup,
	.irq_shutdown = mask_msi_irq,
	.irq_mask = mask_msi_irq,
	.irq_unmask = unmask_msi_irq,
	.irq_ack = bgq_msi_ack_irq,
};

static int bgq_msi_check_device(struct pci_dev *pdev, int nvec, int type)
{
	u64 ctrl;

	ctrl = dcr_read64(bgq_msi.dcr_devbus, BGQ_DCR_DEVBUS_CONTROL);

	if (type == PCI_CAP_ID_MSIX) {
		pr_debug("%s : MSI-X\n", __func__);
		ctrl &= 0x7fffffffffffffffULL;
	} else if (type == PCI_CAP_ID_MSI) {
		pr_debug("%s : MSI\n", __func__);
		ctrl |= 0x8000000000000000ULL;
	}

	dcr_write64(bgq_msi.dcr_devbus, BGQ_DCR_DEVBUS_CONTROL, ctrl);

	return 0;
}


static void bgq_teardown_msi_irqs(struct pci_dev *pdev)
{
	struct msi_desc *entry;

	pr_debug("%s: untested\n", __func__);

	list_for_each_entry(entry, &pdev->msi_list, list) {
		if (entry->irq == NO_IRQ)
			continue;

		irq_set_msi_desc(entry->irq, NULL);
		msi_bitmap_free_hwirqs(&bgq_msi.bitmap, virq_to_hw(entry->irq),
				       1);
		irq_dispose_mapping(entry->irq);
	}

	return;
}

static int bgq_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type)
{
	int rc;
	unsigned int virq;
	struct msi_desc *entry;
	struct msi_msg msg;
	int hwirq;

	pr_info("%s, pdev %p nvec %d type %d\n", __func__, pdev, nvec, type);

	msg.address_hi = 1;
	msg.address_lo = 0;

	rc = msi_bitmap_alloc(&bgq_msi.bitmap, bgq_msi.bitmap_bits,
			      bgq_msi.irq_host->of_node);
	if (rc)
		pr_emerg("msi_bitmap_alloc failed rc = %d\n", rc);

	list_for_each_entry(entry, &pdev->msi_list, list) {
		hwirq = msi_bitmap_alloc_hwirqs(&bgq_msi.bitmap, 1);
		if (hwirq < 0) {
			pr_debug("failed allocating hwirq\n");
			return hwirq;
		}

		virq = irq_create_mapping(bgq_msi.irq_host, hwirq);
		if (virq == NO_IRQ) {
			msi_bitmap_free_hwirqs(&bgq_msi.bitmap, hwirq, 1);
			return -ENOSPC;
		}

		irq_set_msi_desc(virq, entry);
		irq_set_chip(virq, &bgq_msi_irq_chip);
		irq_set_irq_type(virq, IRQ_TYPE_EDGE_RISING);

		msg.data = hwirq;
		write_msi_msg(virq, &msg);
	}

	return 0;
}

static int bgq_msi_map(struct irq_domain *h, unsigned int virq,
		       irq_hw_number_t hw)
{
	/* Insert the interrupt mapping into the radix tree for fast lookup */
	irq_radix_revmap_insert(bgq_msi.irq_host, virq, hw);

	irq_set_chip_data(virq, &bgq_msi_irq_chip);
	irq_set_chip_and_handler_name(virq, &bgq_msi_irq_chip,
				      handle_edge_irq, NULL);
	irq_set_irq_type(virq, IRQ_TYPE_NONE);

	return 0;
}

static int bgq_msi_xlate(struct irq_domain *h, struct device_node *ct,
			 const u32 *intspec, unsigned int intsize,
			 irq_hw_number_t *out_hwirq,
			 unsigned int *out_flags)

{
	pr_info("%s: sz: %u is[0] = %d is[1] = %d\n", __func__, intsize,
		intspec[0], intspec[1]);
	*out_hwirq = intspec[0];
	*out_flags = IRQ_TYPE_EDGE_RISING;

	return 0;
}

static struct irq_domain_ops bgq_msi_ops = {
	.map = bgq_msi_map,
	.xlate = bgq_msi_xlate,
};

void __init bgq_msi_init(dcr_host_t devbus)
{
	struct device_node *dn;
	const u32 *ir;

	pr_info("Initializing BG/Q MSI driver\n");

	dn = of_find_compatible_node(NULL, NULL, "ibm,bgq-msi");
	BUG_ON(!dn);

	bgq_msi.msi_regs = of_iomap(dn, 0);
	BUG_ON(!bgq_msi.msi_regs);

	ir = of_get_property(dn, "msi-interrupt-ranges", NULL);
	BUG_ON(!ir);

	bgq_msi.bitmap_bits = ir[1];

	bgq_msi.dcr_devbus = devbus;

	bgq_msi.irq_host = irq_domain_add_tree(dn, &bgq_msi_ops, &bgq_msi);

	WARN_ON(ppc_md.setup_msi_irqs);
	ppc_md.setup_msi_irqs = bgq_setup_msi_irqs;
	ppc_md.teardown_msi_irqs = bgq_teardown_msi_irqs;
	ppc_md.msi_check_device = bgq_msi_check_device;
}
