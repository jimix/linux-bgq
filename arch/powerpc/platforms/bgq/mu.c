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

#include <asm/udbg.h>

#include "bgq.h"

#define BGQ_DCR_MU_MIN_SYS_ADDR_RANGE 0x0da8
#define BGQ_DCR_MU_MAX_SYS_ADDR_RANGE 0x0db0

static dcr_host_t bgq_dcr_mu;

int bgq_dcr_map(struct device_node *dn, dcr_host_t *hostp)
{
	unsigned dcr_base;
	unsigned dcr_len;
	dcr_host_t host;

	/* get DCR information */
	dcr_base = dcr_resource_start(dn, 0);
	dcr_len = dcr_resource_len(dn, 0);

	/* note, we allow DCR base of 0, GEA is there */
	if (dcr_len == 0) {
		pr_err("%s: couldn't parse dcr properties on %s\n", __func__,
		       dn->full_name);
		return -1;
	}

	host = dcr_map(dn, dcr_base, dcr_len);
	if (DCR_MAP_OK(host)) {
		*hostp = host;
		return 0;
	}
	return -1;
}

int __init bgq_init_mu(phys_addr_t end_of_dram)
{
	struct device_node *dn;
	int rc;

	dn = of_find_compatible_node(NULL, NULL, "ibm,bgq-mu");
	if (!dn) {
		udbg_printf("%s: no devtree node comaptibe with \"ibm,bgq-mu\"",
			    __func__);
		return -1;
	}

	rc = bgq_dcr_map(dn, &bgq_dcr_mu);
	of_node_put(dn);

	if (rc)
		return rc;

	/* Mark the end of physical memory with the memory unit */
	dcr_write64(bgq_dcr_mu, BGQ_DCR_MU_MIN_SYS_ADDR_RANGE, 0);
	dcr_write64(bgq_dcr_mu, BGQ_DCR_MU_MAX_SYS_ADDR_RANGE, end_of_dram);


	return 0;
}
