/*
 * Blue Gene/Q Platform
 * authors:
 *    Jay S. Bryant <jsbryant@us.ibm.com>
 *    Jimi Xenidis <jimix@pobox.com>
 *    Eric Van Hensbergen <ericvh@gmail.com>
 *
 * Based upon arch/powerpc/platforms/pasemi/pci.c
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

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/io.h>

#include <asm/ppc-pci.h>

#include "bgq.h"

/* Workaround for BQC bug 1582 */
DEFINE_SPINLOCK(bgq_arbiter_lock);
#define BGQ_ARBITER_DELAY 10

static inline void bgq_pci_arbiter_sleep(void)
{
	usleep_range(BGQ_ARBITER_DELAY, BGQ_ARBITER_DELAY << 1);
}

/*
 * BGQ supports the extended PCIe configuration space for up to 256
 * buses.  Each bus requires 1 MB of configuration address space
 * resulting in a total configuration space of 256 MB (28 bits).  The
 * memory map allocates 64KB for PCIe configuration space, so accesses
 * to the flat configuration space are accomplished with a windowing
 * approach for offsets above 4 KB.
 *
 * The following two functions enable this process.  The first
 * calculates the prefix while the second writes the prefix out to the
 * hardware.  The result is the 16 upper bits of the configuration
 * space address are written to offset 4096 in the 64K window and
 * subsequent accesses are to offsets 0 to 4095.
 */

static inline u32 bgq_pci_prefix(u8 bus, u8 devfn)
{
	u32 prefix;

	prefix = devfn << 12;
	prefix |= bus << 20;

	return prefix;
}

static inline void bgq_write_prefix(struct pci_controller *hose, u32 prefix)
{
	unsigned __iomem *addr = (hose->cfg_data + 0x1000UL);

	out_be32(addr, prefix);
}

/* Ensure that the offset sent is not larger than 4096. */
static inline int bgq_pci_offset_valid(u8 bus, u8 devfn, int offset)
{
	return offset < 4096;
}

/* Returns the address to read from/write to. */
static void __iomem *bgq_pci_cfg_addr(struct pci_controller *hose,
				      int offset)
{
	return hose->cfg_data + offset;
}

static int bgq_pci_read_config(struct pci_bus *bus, unsigned int devfn,
			       int offset, int len, u32 *val)
{
	unsigned long flags;
	struct pci_controller *hose;
	void __iomem *addr;
	u32 prefix;
	int rc = PCIBIOS_SUCCESSFUL;

	hose = pci_bus_to_host(bus);
	if (!hose)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (!bgq_pci_offset_valid(bus->number, devfn, offset))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	spin_lock_irqsave(&bgq_arbiter_lock, flags);

	prefix = bgq_pci_prefix(bus->number, devfn);
	bgq_write_prefix(hose, prefix);
	addr = bgq_pci_cfg_addr(hose, offset);

	switch (len) {
	case 1:
		*val = in_8(addr);
		break;
	case 2:
		*val = in_le16(addr);
		break;
	case 4:
		*val = in_le32(addr);
		break;
	default:
		rc = PCIBIOS_FUNC_NOT_SUPPORTED;
		break;
	}

	spin_unlock_irqrestore(&bgq_arbiter_lock, flags);

	pr_debug("%s(bus %u, devfn 0x%x, offset 0x%x, len %u)=0x%08x\n",
		 __func__, bus->number, devfn, offset, len, *val);

	return rc;
}

static int bgq_pci_write_config(struct pci_bus *bus, unsigned int devfn,
				int offset, int len, u32 val)
{
	struct pci_controller *hose;
	void __iomem *addr;
	unsigned long flags;
	u32 prefix;
	int rc = PCIBIOS_SUCCESSFUL;

	hose = pci_bus_to_host(bus);
	if (!hose)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (!bgq_pci_offset_valid(bus->number, devfn, offset))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	pr_debug("%s(bus %u, devfn 0x%x, offset 0x%x, len %d, value 0x%x)\n",
		 __func__, bus->number, devfn, offset, len, val);

	/*
	 * Workaround for BQC bug 1582.	 Grab the lock so that no
	 * other threads can perform PCI config writes at the same
	 * time.  Deactivate all hardware threads except the current
	 * one.	 Pause to allow the arbiter to recover from any
	 * outbound PCI memory writes that might have happened
	 * recently.  Perform PCI configuration write.	Pause again to
	 * allow arbiter to recover from the configuration write.
	 * Re-enable any threads that were active before.  Release the
	 * lock.  */
	spin_lock_irqsave(&bgq_arbiter_lock, flags);

	/*
	 * Pause to allow the arbiter to recover from any outbound PCI
	 * memory transactions.
	 */
	bgq_pci_arbiter_sleep();

	/* Perform the PCI configuration write. */
	prefix = bgq_pci_prefix(bus->number, devfn);
	bgq_write_prefix(hose, prefix);

	addr = bgq_pci_cfg_addr(hose, offset);

	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	switch (len) {
	case 1:
		out_8(addr, val);
		break;
	case 2:
		out_le16(addr, val);
		break;
	case 4:
		out_le32(addr, val);
		break;
	default:
		rc = PCIBIOS_FUNC_NOT_SUPPORTED;
	}

	/*
	 * Workaround for BQC bug 1582.	 Pause to allow the arbiter to
	 * recover from the configuration write.
	 */
	bgq_pci_arbiter_sleep();

	/* Release the lock. */
	spin_unlock_irqrestore(&bgq_arbiter_lock, flags);

	return rc;
}

static struct pci_ops bgq_pci_ops = {
	.read = bgq_pci_read_config,
	.write = bgq_pci_write_config,
};

static int __init bgq_setup_one_phb(struct device_node *np)
{
	int rc = 0;
	struct pci_controller *hose;

	pr_debug("PCI: Configuring host bridge %s\n", np->full_name);

	hose = pcibios_alloc_controller(np);
	BUG_ON(!hose);

	hose->ops = &bgq_pci_ops;
	hose->first_busno = 0;
	hose->last_busno = 0xff;

	hose->cfg_data = of_iomap(np, 0);
	BUG_ON(!hose->cfg_data);

	pci_process_bridge_OF_ranges(hose, np, 0);

	pci_add_flags(PCI_REASSIGN_ALL_BUS | PCI_REASSIGN_ALL_RSRC |
		      PCI_ENABLE_PROC_DOMAINS);

	return rc;
}

void __init bgq_pci_init(void)
{
	struct device_node *np;
	int rc;

	for_each_compatible_node(np, "pciex", "ibm,bgq-pciex") {
		rc = bgq_setup_one_phb(np);
		if (rc)
			pr_err("Failure configuring PCIe bridge %s, rc=%d\n",
			       np->full_name, rc);
	}

	pci_devs_phb_init();
}

#define BGQ_DCR_PCIE_INTERRUPT_STATE	0x014
void __devinit bgq_pcibios_fixup(void)
{
	struct device_node *dn;
	dcr_host_t dcr_pcie;
	unsigned dcr_base;
	unsigned dcr_len;

	/*
	 * Cleanup after the bus walk.  The bus probing reads invalid
	 * bus/function addresses during the probe which raises
	 * interrupt conditions.
	 */
	for_each_compatible_node(dn, "pciex", "ibm,bgq-pciex") {
		/* get DCR information */
		dcr_base = dcr_resource_start(dn, 0);
		dcr_len = dcr_resource_len(dn, 0);
		BUG_ON(dcr_len == 0);

		dcr_pcie = dcr_map(dn, dcr_base, dcr_len);
		if (!DCR_MAP_OK(dcr_pcie))
			panic("%s:dcr_map failed for %s\n", __func__,
			      dn->full_name);

		dcr_write64(dcr_pcie, BGQ_DCR_PCIE_INTERRUPT_STATE, ~0ULL);

		/* we will never use this again */
		dcr_unmap(dcr_pcie, dcr_len);
	}
}

void __devinit bgq_dma_dev_setup(struct pci_dev *dev)
{
	dev->dev.archdata.dma_ops = &dma_direct_ops;
	dev->dev.archdata.dma_data.dma_offset = 0x0004000000000000ULL;
}
