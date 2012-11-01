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

#include <linux/of_platform.h>
#include <linux/root_dev.h>
#include <linux/linkage.h>
#include <linux/uaccess.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <linux/time.h>
#include <linux/memblock.h>

#include <asm/cputhreads.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/reg_a2.h>

#include "bgq.h"

static int __init bgq_probe_devices(void)
{
	static __initdata struct of_device_id bus_ids[] = {
		{ .compatible = "ibm,bgq-soc", },
		{},
	};

	int rc = of_platform_bus_probe(NULL, bus_ids, NULL);

	return rc;
}
machine_arch_initcall(bgq, bgq_probe_devices);

/*
 * Called after basic CPU initialization.  Install the interrupt
 * vector if this is the first thread on the core.
 */
void __devinit bgq_setup_cpu(int cpu)
{
	u32 ccr2;

	/* Only do this for the primary thread of a core */
	if (cpu != cpu_first_thread_sibling(cpu))
		return;

	/* Disable some A2 features that BGQ does not support */
	ccr2 = mfspr(SPRN_A2_CCR2);
	ccr2 &= ~A2_CCR2_ENABLE_PC;
	ccr2 &= ~A2_CCR2_ENABLE_ICSWX;
	mtspr(SPRN_A2_CCR2, ccr2);
}

static int __init bgq_early_scan_config(unsigned long node, const char *uname,
					int depth, void *data)
{
	struct memblock_region *mbr = data;
	u64 *p;

	p = of_get_flat_dt_prop(node, "ibm,bgq-config-start", NULL);
	if (!p)
		return 0;

	mbr->base = *p;

	p = of_get_flat_dt_prop(node, "ibm,bgq-config-size", NULL);
	if (!p)
		return 0;

	mbr->size = *p;

	return 1;
}

/*
 * bgq_probe() is called very early only on CPU 0.  The dev tree isn't
 * flattened yet.  Verify that boot wrapper says we are Blue Gene/Q,
 * and reserve the config space, if it is descrived in the devtree.
 */
static int __init bgq_probe(void)
{
	ulong root = of_get_flat_dt_root();
	struct memblock_region mbr;

	if (!of_flat_dt_is_compatible(root, "ibm,bluegeneq"))
		return 0;

	if (of_scan_flat_dt(bgq_early_scan_config, &mbr))
		memblock_reserve(mbr.base, mbr.size);

	return 1;
}

static void __init bgq_setup_arch(void)
{
	if (bgq_init_mu(memblock_end_of_DRAM()))
		bgq_panic("Could not init the MU\n");

#ifdef CONFIG_SMP
	bgq_setup_smp();
#endif
	/*
	 * We can use 0 here since all we are doing is figuring out if
	 * we are the first thread of our core.
	 */
	bgq_setup_cpu(0);

#ifdef CONFIG_PCI
	/* call bgq_pci_init(); here */
#endif
}

static void __init bgq_init_early(void)
{
	bgq_mailbox_init();
	udbg_init_bgq_early();
}

define_machine(bgq) {
	.name		= "Blue Gene/Q",
	.probe		= bgq_probe,
	.setup_arch	= bgq_setup_arch,
	.init_early	= bgq_init_early,
	.init_IRQ	= bgq_init_IRQ,
	.power_save	= book3e_idle,
	.get_irq	= NULL,
	.restart	= bgq_restart,
	.power_off	= bgq_halt,
	.halt		= bgq_halt,
	.panic		= bgq_panic,
	.calibrate_decr	= generic_calibrate_decr,
	.progress	= udbg_progress,
};
