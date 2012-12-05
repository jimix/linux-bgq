/*
 * Blue Gene/Q Platform
 * authors:
 *    Jay S. Bryant <jsbryant@us.ibm.com>
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

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/of.h>

#include <platforms/bgq/personality.h>

#define BGQ_PERSDEV_MINOR 111
#define BGQ_PERSDEV_NAME "bgpers"

enum bgq_config_names {
	machine_mtu = 0,
	machine_ipv4netmask,
	machine_ipv6prefix,
	machine_ipv4broadcast,
	machine_servicenode_ipv4address,
	machine_servicenode_ipv6address = 5,
	machine_ipv4gateway,
	machine_bgsys_ipv4address,
	machine_bgsys_ipv6address,
	machine_bgsys_remotepath,
	machine_bgsys_mountoptions = 10,
	machine_distro_ipv4address,
	machine_distro_ipv6address,
	machine_distro_remotepath,
	machine_distro_mountoptions,
	bgsys_ipv4address = 15,
	bgsys_ipv6address,
	bgsys_remotepath,
	bgsys_mountoptions,
	distro_ipv4address,
	distro_ipv6address = 20,
	distro_remotepath,
	distro_mountoptions,
	servicenode_ipv4address,
	servicenode_ipv6address,
	external1_name = 25,
	external1_ipv4address,
	external1_ipv6address,
	external1_ipv4netmask,
	external1_ipv6prefix,
	external1_ipv4broadcast = 30,
	external1_mtu,
	external2_name,
	external2_ipv4address,
	external2_ipv6address,
	external2_ipv4netmask = 35,
	external2_ipv6prefix,
	external2_ipv4broadcast,
	external2_mtu = 38,
};

static const char bgq_def_ipv4_addr[] = "0.0.0.0";
static const char bgq_def_ipv6_addr[] = "0:0:0:0:0:0:0:0";
static const char bgq_def_empty[] = "\0";

struct bgq_config {
	const char *name;
	const char *value;
};

#define BGQ_SET_CONFIG(n, v) { .name = # n, .value = v }
/* This will get thrown out after module init */
static __initdata struct bgq_config bgq_defconfig[] = {
	BGQ_SET_CONFIG(machine_mtu, bgq_def_empty),
	BGQ_SET_CONFIG(machine_ipv4netmask, bgq_def_ipv4_addr),
	BGQ_SET_CONFIG(machine_ipv6prefix, bgq_def_empty),
	BGQ_SET_CONFIG(machine_ipv4broadcast, bgq_def_ipv4_addr),
	BGQ_SET_CONFIG(machine_servicenode_ipv4address, bgq_def_ipv4_addr),
	BGQ_SET_CONFIG(machine_servicenode_ipv6address, bgq_def_ipv6_addr),
	BGQ_SET_CONFIG(machine_ipv4gateway, bgq_def_ipv4_addr),
	BGQ_SET_CONFIG(machine_bgsys_ipv4address, bgq_def_ipv4_addr),
	BGQ_SET_CONFIG(machine_bgsys_ipv6address, bgq_def_ipv6_addr),
	BGQ_SET_CONFIG(machine_bgsys_remotepath, bgq_def_empty),
	BGQ_SET_CONFIG(machine_bgsys_mountoptions, bgq_def_empty),
	BGQ_SET_CONFIG(machine_distro_ipv4address, bgq_def_ipv4_addr),
	BGQ_SET_CONFIG(machine_distro_ipv6address, bgq_def_ipv6_addr),
	BGQ_SET_CONFIG(machine_distro_remotepath, bgq_def_empty),
	BGQ_SET_CONFIG(machine_distro_mountoptions, bgq_def_empty),
	BGQ_SET_CONFIG(bgsys_ipv4address, bgq_def_ipv4_addr),
	BGQ_SET_CONFIG(bgsys_ipv6address, bgq_def_ipv6_addr),
	BGQ_SET_CONFIG(bgsys_remotepath, bgq_def_empty),
	BGQ_SET_CONFIG(bgsys_mountoptions, bgq_def_empty),
	BGQ_SET_CONFIG(distro_ipv4address, bgq_def_ipv4_addr),
	BGQ_SET_CONFIG(distro_ipv6address, bgq_def_ipv6_addr),
	BGQ_SET_CONFIG(distro_remotepath, bgq_def_empty),
	BGQ_SET_CONFIG(distro_mountoptions, bgq_def_empty),
	BGQ_SET_CONFIG(servicenode_ipv4address, bgq_def_ipv4_addr),
	BGQ_SET_CONFIG(servicenode_ipv6address, bgq_def_ipv6_addr),
	BGQ_SET_CONFIG(external1_name, bgq_def_empty),
	BGQ_SET_CONFIG(external1_ipv4address, bgq_def_ipv4_addr),
	BGQ_SET_CONFIG(external1_ipv6address, bgq_def_ipv6_addr),
	BGQ_SET_CONFIG(external1_ipv4netmask, bgq_def_ipv4_addr),
	BGQ_SET_CONFIG(external1_ipv6prefix, bgq_def_empty),
	BGQ_SET_CONFIG(external1_ipv4broadcast, bgq_def_ipv4_addr),
	BGQ_SET_CONFIG(external1_mtu, bgq_def_empty),
	BGQ_SET_CONFIG(external2_name, bgq_def_empty),
	BGQ_SET_CONFIG(external2_ipv4address, bgq_def_ipv4_addr),
	BGQ_SET_CONFIG(external2_ipv6address, bgq_def_ipv6_addr),
	BGQ_SET_CONFIG(external2_ipv4netmask, bgq_def_ipv4_addr),
	BGQ_SET_CONFIG(external2_ipv6prefix, bgq_def_empty),
	BGQ_SET_CONFIG(external2_ipv4broadcast, bgq_def_ipv4_addr),
	BGQ_SET_CONFIG(external2_mtu, bgq_def_empty),
};

struct bgq_pers_el {
	struct list_head list;
	unsigned len;
	char line[0];
};

static LIST_HEAD(bgq_pers_list);

#define BGQ_UCI_COMPONENT_COMPUTE_CARD_ON_NODE_BOARD			13
#define BGQ_UCI_COMPONENT_COMPUTE_CARD_ON_IO_BOARD_ON_COMPUTE_RACK	30
#define MASK(s, e) ((~0ULL << (63 - (e))) & (~0ULL >> (s)))
#define UCI_GET(v, s, e) ((((u64)(v)) & MASK(s, e)) >> (63 - (e)))

static int bgq_pers_uci(u64 uci, char *buf, unsigned sz)
{
	int component;
	int rc;
	static const char const digits[] =
		"0123456789ABCDEFGHIJKLMNOPQRTSUVWXYZ";

	component = UCI_GET(uci, 0, 5);
	switch (component) {
	case BGQ_UCI_COMPONENT_COMPUTE_CARD_ON_NODE_BOARD:
		rc = snprintf(buf, sz, "R%c%c-M%llu-N%02llu-J%02llu",
			      /* row */
			      digits[UCI_GET(uci, 6, 10)],
			      /* col */
			      digits[UCI_GET(uci, 11, 15)],
			      /* midplane */
			      UCI_GET(uci, 16, 16),
			      /* node board */
			      UCI_GET(uci, 17, 20),
			      /* compute card */
			      UCI_GET(uci, 21, 25));
		break;
	case BGQ_UCI_COMPONENT_COMPUTE_CARD_ON_IO_BOARD_ON_COMPUTE_RACK:
		rc = snprintf(buf, sz, "R%c%c-I%c-J%02llu",
			      /* row */
			      digits[UCI_GET(uci, 6, 10)],
			      /* col */
			      digits[UCI_GET(uci, 11, 15)],
			      /* IO board */
			      digits[UCI_GET(uci, 16, 19)],
			      /* compute card */
			      UCI_GET(uci, 21, 25));
		break;
	default:
		rc = snprintf(buf, sz, "Q%c%c-I%c-J%02llu",
			      /* row */
			      digits[UCI_GET(uci, 6, 10)],
			      /* col */
			      digits[UCI_GET(uci, 11, 15)],
			      /* node board */
			      digits[UCI_GET(uci, 16, 19)],
			      /* compute card */
			      UCI_GET(uci, 21, 25));
		break;
	}
	return rc;
}

static int bgq_pers_bit(const char *var, u64 val, u64 bit)
{
	unsigned sz;
	struct bgq_pers_el *el;
	int b;

	sz = strlen(var);
	sz += 1;		/* bit */
	sz += 3;		/* = + \n + Nul */

	if (val & bit)
		b = 1;
	else
		b = 0;

	el = kmalloc(sizeof(*el) + sz, GFP_KERNEL);
	if (el) {
		el->len = sz;
		sprintf(el->line, "%s=%d\n", var, b);
		list_add_tail(&el->list, &bgq_pers_list);
		return 0;
	}
	return -ENOMEM;
}

static int bgq_pers_val(const char *var, u64 val, int hex)
{
	unsigned sz;
	struct bgq_pers_el *el;

	sz = strlen(var);
	sz += 20;		/* digits */
	sz += 3;		/* = + \n + Nul */

	el = kmalloc(sizeof(*el) + sz, GFP_KERNEL);
	if (el) {
		el->len = sz;
		if (hex)
			sprintf(el->line, "%s=%llx\n", var, val);
		else
			sprintf(el->line, "%s=%llu\n", var, val);

		list_add_tail(&el->list, &bgq_pers_list);
		return 0;
	}
	return -ENOMEM;
}

static int bgq_pers_str(const char *var, const char *val, int quote)
{
	unsigned sz;
	struct bgq_pers_el *el;

	sz = strlen(var);
	sz += strlen(val);
	sz += 5;		/* "" + = + '\n' + Nul */

	el = kmalloc(sizeof(*el) + sz, GFP_KERNEL);
	if (el) {
		el->len = sz;
		if (quote)
			sprintf(el->line, "%s=\"%s\"\n", var, val);
		else
			sprintf(el->line, "%s=%s\n", var, val);

		list_add_tail(&el->list, &bgq_pers_list);
		return 0;
	}
	return -ENOMEM;
}

static int bgq_pers_make_list(unsigned ents, struct bgq_config *lc,
			      struct bgq_personality *p)
{
	struct bgq_personality_kernel *kc = &p->kernel_config;
	struct bgq_personality_networks *nc = &p->network_config;
	int isio;
	u64 kn;
	/* temporary values and buffers */
	char buf[128];
	u64 v;

	kn = kc->node_config;
	isio = kn & PERS_ENABLE_IsIoNode;

	/*
	 * Use CPP __COUNTER__ to give us a clue as to which the many
	 * calls below is failing.  The Macro starts at zero and that
	 * means success, so we throw out the first one.
	 */
	v = __COUNTER__;

	/*
	 * Node information
	 */
	if (bgq_pers_val("BG_UCI", kc->uci, 1))
		return __COUNTER__;

	bgq_pers_uci(kc->uci, buf, sizeof(buf));
	if (bgq_pers_str("BG_LOCATION", buf, 0))
		return __COUNTER__;

	if (p->network_config.block_id) {
		if (bgq_pers_val("BG_BLOCKID", nc->block_id, 0))
			return __COUNTER__;
	}

	/*
	 * Gateway and SN
	 */
	if (bgq_pers_str("BG_GATEWAY", lc[machine_ipv4gateway].value, 0))
		return __COUNTER__;
	if (bgq_pers_str("BG_IPV4_SN", lc[servicenode_ipv4address].value, 0))
		return __COUNTER__;
	if (bgq_pers_str("BG_IPV6_SN", lc[servicenode_ipv6address].value, 0))
		return __COUNTER__;

	/*
	 * /bgsys and distrofs info
	 */
	if (bgq_pers_str("BG_BGSYS_IPV4", lc[bgsys_ipv4address].value, 0))
		return __COUNTER__;
	if (bgq_pers_str("BG_BGSYS_IPV6", lc[bgsys_ipv6address].value, 0))
		return __COUNTER__;
	if (bgq_pers_str("BG_BGSYS_EXPORT_DIR", lc[bgsys_remotepath].value, 1))
		return __COUNTER__;
	if (bgq_pers_str("BG_BGSYS_EXPORT_MOUNT_OPTS",
			 lc[bgsys_mountoptions].value, 1))
		return __COUNTER__;
	if (bgq_pers_str("BG_DISTRO_IPV4", lc[distro_ipv4address].value, 0))
		return __COUNTER__;
	if (bgq_pers_str("BG_DISTRO_IPV6", lc[distro_ipv6address].value, 0))
		return __COUNTER__;
	if (bgq_pers_str("BG_DISTRO_EXPORT_DIR",
			 lc[distro_remotepath].value, 1))
		return __COUNTER__;
	if (bgq_pers_str("BG_DISTRO_MOUNT_OPTS",
			 lc[distro_mountoptions].value, 1))
		return __COUNTER__;

	/*
	 * Interface0 (External1)
	 */
	if (bgq_pers_str("BG_INTF0_NAME", lc[external1_name].value, 0))
		return __COUNTER__;
	if (bgq_pers_str("BG_INTF0_IPV4", lc[external1_ipv4address].value, 0))
		return __COUNTER__;
	if (bgq_pers_str("BG_INTF0_IPV6", lc[external1_ipv6address].value, 0))
		return __COUNTER__;
	if (bgq_pers_str("BG_INTF0_NETMASK",
			 lc[external1_ipv4netmask].value, 0))
		return __COUNTER__;
	if (bgq_pers_str("BG_INTF0_IPV6_PREFIX",
			 lc[external1_ipv6prefix].value, 0))
		return __COUNTER__;
	if (bgq_pers_str("BG_INTF0_BROADCAST",
			 lc[external1_ipv4broadcast].value, 0))
		return __COUNTER__;
	if (bgq_pers_str("BG_INTF0_MTU", lc[external1_mtu].value, 0))
		return __COUNTER__;

	/*
	 * Interface1 (External2)
	 */
	if (bgq_pers_str("BG_INTF1_NAME", lc[external2_name].value, 0))
		return __COUNTER__;
	if (bgq_pers_str("BG_INTF1_IPV4", lc[external2_ipv4address].value, 0))
		return __COUNTER__;
	if (bgq_pers_str("BG_INTF1_IPV6", lc[external2_ipv6address].value, 0))
		return __COUNTER__;
	if (bgq_pers_str("BG_INTF1_NETMASK",
			 lc[external2_ipv4netmask].value, 0))
		return __COUNTER__;
	if (bgq_pers_str("BG_INTF1_IPV6_PREFIX",
			 lc[external2_ipv6prefix].value, 0))
		return __COUNTER__;
	if (bgq_pers_str("BG_INTF1_BROADCAST",
			 lc[external2_ipv4broadcast].value, 0))
		return __COUNTER__;
	if (bgq_pers_str("BG_INTF1_MTU", lc[external2_mtu].value, 0))
		return __COUNTER__;

	/*
	 * tor0
	 */
	if (bgq_pers_str("BG_TOR0_NAME", "tor0", 0))
		return __COUNTER__;

	{
		/*
		 * Calculate the MAC address.  For IONs the MAC starts
		 * with 0x66 and the rest of the bits are created using
		 * the 5D torus coordinates.
		 */
		u8 hwaddr[6] = {
			0x42, nc->a_coord, nc->b_coord, nc->c_coord,
			nc->d_coord, nc->e_coord,
		};

		if (isio)
			hwaddr[0] = 0x66;

		sprintf(buf, "%pM", hwaddr);
		if (bgq_pers_str("BG_TOR0_MAC", buf, 0))
			return __COUNTER__;
	}
	{
		/*
		 * Fetch the node torus coordinates to generate tor0
		 * interface information.
		 *
		 * The IP address is in 10.0.0.0/8 (255.0.0.0 mask)
		 * for the tor0 interface.
		 *
		 * Design documentation may be found in issue 1916
		 *
		 * 00001010  nnppaaaa bbbbcccc ddddeeee
		 *
		 * nn = 01 for an ION or 10 for Linux running on a
		 *	compute node
		 * pp = 00 for ND_CN_BRIDGE_PORT_10 or undefined.
		 *	01 for ND_CN_BRIDGE_PORT_6
		 *	10 for ND_CN_BRIDGE_PORT_7
		 * a,b,c,d and e are the node's 5D torus coordinates.
		 *****************************************************/
		unsigned ipv4_addr;

		ipv4_addr = 10;
		ipv4_addr <<= 2;
		if (isio)
			ipv4_addr |= 1;
		else
			ipv4_addr |= 2;
		ipv4_addr <<= 2;

		if (nc->net_flags2 & ND_CN_BRIDGE_PORT_6)
			ipv4_addr |= 1;
		else if (nc->net_flags2 & ND_CN_BRIDGE_PORT_7)
			ipv4_addr |= 2;
		ipv4_addr <<= 4;

		ipv4_addr |= nc->a_coord;
		ipv4_addr <<= 4;
		ipv4_addr |= nc->b_coord;
		ipv4_addr <<= 4;
		ipv4_addr |= nc->c_coord;
		ipv4_addr <<= 4;
		ipv4_addr |= nc->d_coord;
		ipv4_addr <<= 4;
		ipv4_addr |= nc->e_coord;

		sprintf(buf, "%pI4", &ipv4_addr);
		if (bgq_pers_str("BG_TOR0_IPV4", buf, 0))
			return __COUNTER__;
	}
	if (bgq_pers_str("BG_TOR0_NETMASK", "255.0.0.0", 0))
		return __COUNTER__;

	if (bgq_pers_str("BG_TOR0_BROADCAST", "10.255.255.255", 0))
		return __COUNTER__;

	if (bgq_pers_val("BG_TOR0_MTU", 512, 0))
		return __COUNTER__;

	/*
	 * Node Mode Data
	 */
	if (kn &
	    (PERS_ENABLE_Mambo | PERS_ENABLE_FPGA | PERS_ENABLE_Simulation))
		v = 1;
	else
		v = 0;
	if (bgq_pers_val("BG_IS_SIMULATION", v, 0))
		return __COUNTER__;

	if (bgq_pers_str("BG_NODE_TYPE", (isio) ? "ION" : "CN", 0))
		return __COUNTER__;

	/*
	 * Node Location and connections
	 */
	sprintf(buf, "<%d,%d,%d,%d,%d>", nc->a_coord, nc->b_coord,
		nc->c_coord, nc->d_coord, nc->e_coord);
	if (bgq_pers_str("BG_NODE_COORDS", buf, 1))
		return __COUNTER__;

	sprintf(buf, "<%d,%d,%d,%d,%d>", nc->a_nodes, nc->b_nodes,
		nc->c_nodes, nc->d_nodes, nc->e_nodes);
	if (bgq_pers_str("BG_NODE_DIMENSIONS", buf, 1))
		return __COUNTER__;

	sprintf(buf, "<%d,%d,%d,%d,%d>", nc->cn_bridge_a, nc->cn_bridge_b,
		nc->cn_bridge_c, nc->cn_bridge_d, nc->cn_bridge_e);
	if (bgq_pers_str("BG_NODE_COMPUTE_NODE_BRIDGE", buf, 1))
		return __COUNTER__;

	if (nc->net_flags2 & ND_CN_BRIDGE_PORT_10)
		v = 10;
	else if (nc->net_flags2 & ND_CN_BRIDGE_PORT_6)
		v = 6;
	else if (nc->net_flags2 & ND_CN_BRIDGE_PORT_7)
		v = 7;
	else
		v = 0;
	if (bgq_pers_val("BG_NODE_CN_BRIDGE_PORT", v, 1))
		return __COUNTER__;

	if (nc->net_flags2 & TI_USE_PORT6_FOR_IO)
		v = 6;
	else if (nc->net_flags2 & TI_USE_PORT7_FOR_IO)
		v = 7;
	else
		v = 0;
	if (bgq_pers_val("BG_NODE_PORT_FOR_IO", v, 1))
		return __COUNTER__;

	/*
	 * Other Node Config Data
	 */
	if (bgq_pers_bit("BG_NODE_ENABLE_MAMBO", kn, PERS_ENABLE_Mambo))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_FPGA", kn, PERS_ENABLE_FPGA))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_CYCLESIM", kn,
			 PERS_ENABLE_Simulation))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_CIOS", kn,
			 PERS_ENABLE_IOServices))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_MMU", kn, PERS_ENABLE_MMU))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_TAKECPU", kn, PERS_ENABLE_TakeCPU))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_MU", kn, PERS_ENABLE_MU))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_ND", kn, PERS_ENABLE_ND))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_TIMESTAMPS", kn,
			 PERS_ENABLE_Timestamps))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_BEDRAM", kn,
			 PERS_ENABLE_BeDRAM))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_CLOCKSTOP", kn,
			 PERS_ENABLE_ClockStop))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_DRARBITER", kn,
			 PERS_ENABLE_DrArbiter))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_DEVBUS", kn, PERS_ENABLE_DevBus))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_L1P", kn, PERS_ENABLE_L1P))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_L2", kn, PERS_ENABLE_L2))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_MSGC", kn, PERS_ENABLE_MSGC))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_TESTINT", kn, PERS_ENABLE_TestInt))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_NODEREPRO", kn,
			 PERS_ENABLE_NodeRepro))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_PARTITIONREPRO", kn,
			 PERS_ENABLE_PartitionRepro))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_DD1_WORKAROUNDS", kn,
			 PERS_ENABLE_DD1_Workarounds))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_A2_ERRATA", kn,
			 PERS_ENABLE_A2_Errata))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_A2_IU_LLB", kn,
			 PERS_ENABLE_A2_IU_LLB))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_A2_XU_LLB", kn,
			 PERS_ENABLE_A2_XU_LLB))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_DDRCALIBRATION", kn,
			 PERS_ENABLE_DDRCalibration))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_DDRFASTINIT", kn,
			 PERS_ENABLE_DDRFastInit))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_APPPRELOAD", kn,
			 PERS_ENABLE_AppPreload))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_SPECCAPDDR", kn,
			 PERS_ENABLE_SpecCapDDR))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_L2ONLY", kn, PERS_ENABLE_L2Only))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_DDRINIT", kn, PERS_ENABLE_DDRINIT))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_MAILBOX", kn, PERS_ENABLE_Mailbox))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_JTAGCONSOLE", kn,
			 PERS_ENABLE_JTagConsole))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_JTAGLOADER", kn,
			 PERS_ENABLE_JTagLoader))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_FPU", kn, PERS_ENABLE_FPU))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_L2COUNTERS", kn,
			 PERS_ENABLE_L2Counters))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_WAKEUP", kn, PERS_ENABLE_Wakeup))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_BIC", kn, PERS_ENABLE_BIC))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_DDR", kn, PERS_ENABLE_DDR))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_GLOBALINTS", kn,
			 PERS_ENABLE_GlobalInts))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_SERDES", kn,
			 PERS_ENABLE_SerDes))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_UPC", kn, PERS_ENABLE_UPC))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_ENVMON", kn, PERS_ENABLE_EnvMon))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_PCIE", kn, PERS_ENABLE_PCIe))
		return __COUNTER__;
	if (bgq_pers_bit("BG_NODE_ENABLE_DIAGNOSTICSMODE", kn,
			 PERS_ENABLE_DiagnosticsMode))
		return __COUNTER__;
	if (bgq_pers_val("BG_DDR_SIZE_MB", p->ddr_config.ddr_size_mb, 0))
		return __COUNTER__;
	if (bgq_pers_val("BG_CLOCK_MHZ", kc->freq_mhz, 0))
		return __COUNTER__;

	/*
	 * Append additional config service values.
	 */
	if (ents > ARRAY_SIZE(bgq_defconfig)) {
		int i;
		int j;

		for (i = ARRAY_SIZE(bgq_defconfig); i < ents; i++) {
			if (!lc[i].name)
				continue;

			/* make it upper case */
			j = 0;
			while (lc[i].name[j] != '\0') {
				buf[j] = toupper(lc[i].name[j]);
				j++;
			}
			buf[j] = '\0';
			if (bgq_pers_str(buf, lc[i].value, 1))
				return __COUNTER__;
		}
	}
	return 0;
}

static void bgq_pers_free_list(void)
{
	struct bgq_pers_el *el;

	list_for_each_entry(el, &bgq_pers_list, list) {
		list_del(&el->list);
		kfree(el);
	}
}

/* Functions for seqential access of the bgpers structure */
static void *bgq_persdev_seq_start(struct seq_file *f, loff_t *pos)
{
	return seq_list_start(&bgq_pers_list, *pos);
}

static void *bgq_persdev_seq_next(struct seq_file *f, void *v, loff_t *pos)
{
	return seq_list_next(v, &bgq_pers_list, pos);
}

static void bgq_persdev_seq_stop(struct seq_file *f, void *v)
{
	return;
}

static int bgq_persdev_seq_show(struct seq_file *f, void *v)
{
	const struct bgq_pers_el *el;

	el = list_entry(v, typeof(*el), list);
	seq_puts(f, el->line);

	return 0;
}

/* File/Device structure declarations */
static const struct seq_operations bgq_persdev_seq_ops = {
	.start = bgq_persdev_seq_start,
	.next = bgq_persdev_seq_next,
	.stop = bgq_persdev_seq_stop,
	.show = bgq_persdev_seq_show
};

static int bgq_persdev_open(struct inode *inode, struct file *f)
{
	f->private_data = NULL;
	return seq_open(f, &bgq_persdev_seq_ops);
}

static const struct file_operations bgq_persdev_fops = {
	.owner = THIS_MODULE,
	.open = bgq_persdev_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};

static struct miscdevice bgq_persdev = {
	.minor = BGQ_PERSDEV_MINOR,
	.name = BGQ_PERSDEV_NAME,
	.fops = &bgq_persdev_fops
};

static void __init bgq_add_config(struct bgq_config *lc, unsigned ents,
				  const char *name, const char *value)
{
	unsigned i;
	int empty = -1;

	for (i = 0; i < ents; i++) {
		if (lc[i].name == NULL) {
			if (empty == -1)
				empty = i;
			continue;
		}

		if (strcmp(name, lc[i].name) == 0) {
			lc[i].value = value;

			/* special handeling for some */
			switch (i) {
			case machine_mtu:
				lc[external1_mtu].value = value;
				lc[external2_mtu].value = value;
				break;
			case machine_ipv4netmask:
				lc[external1_ipv4netmask].value = value;
				lc[external2_ipv4netmask].value = value;
				break;
			case machine_ipv6prefix:
				lc[external1_ipv6prefix].value = value;
				lc[external2_ipv6prefix].value = value;
				break;
			case machine_ipv4broadcast:
				lc[external1_ipv4broadcast].value = value;
				lc[external2_ipv4broadcast].value = value;
				break;
			case machine_servicenode_ipv4address:
				lc[servicenode_ipv4address].value = value;
				break;
			case machine_servicenode_ipv6address:
				lc[servicenode_ipv6address].value = value;
				break;
			case machine_bgsys_ipv4address:
				lc[bgsys_ipv4address].value = value;
				break;
			case machine_bgsys_ipv6address:
				lc[bgsys_ipv6address].value = value;
				break;
			case machine_bgsys_remotepath:
				lc[bgsys_remotepath].value = value;
				break;
			case machine_bgsys_mountoptions:
				lc[bgsys_mountoptions].value = value;
				break;
			case machine_distro_ipv4address:
				lc[distro_ipv4address].value = value;
				break;
			case machine_distro_ipv6address:
				lc[distro_ipv6address].value = value;
				break;
			case machine_distro_remotepath:
				lc[distro_remotepath].value = value;
				break;
			case machine_distro_mountoptions:
				lc[distro_mountoptions].value = value;
				break;
			}
			return;
		}
	}
	BUG_ON(empty == -1);
	lc[empty].name = name;
	lc[empty].value = value;
}

static int __init bgq_persdev_module_init(void)
{
	struct device_node *dn;
	const u64 *prop;
	struct bgq_config *local_config;
	struct bgq_personality pers;
	int pers_sz;
	const char *config_base;
	ulong config_sz;
	char *config;
	char *data;
	char *end;
	char *var_name;
	unsigned ents;
	unsigned i;
	int rc;

	pr_info("Initializing Blue Gene/Q Personality Device\n");

	dn = of_find_compatible_node(NULL, NULL, "ibm,bgq-soc");
	if (!dn) {
		pr_info("%s: No BG/Q node\n", __func__);
		return -ENODEV;
	}
	prop = of_get_property(dn, "ibm,bgq-config-start", NULL);
	if (!prop) {
		pr_emerg("%s: No BG/Q config-start\n", __func__);
		return -ENODEV;
	}
	config_base = __va(*prop);

	prop = of_get_property(dn, "ibm,bgq-config-size", NULL);
	if (!prop) {
		pr_emerg("%s: No BG/Q config-size\n", __func__);
		return -ENODEV;
	}
	config_sz = *prop;

	prop = of_get_property(dn, "ibm,bgq-personality", &pers_sz);
	if (!prop) {
		pr_emerg("%s: No BG/Q personality\n", __func__);
		return -ENODEV;
	}
	if (pers_sz != sizeof(pers)) {
		pr_emerg("%s: BG/Q personality bad size, got %d, expect %ld\n",
			 __func__, pers_sz, sizeof(pers));
		return -ENODEV;
	}
	memcpy(&pers, prop, sizeof(pers));

	ents = 0;
	config = NULL;
	data = NULL;
	if (isprint(config_base[0]) && isprint(config_base[1])) {
		/* Make a copy of config so we keep the original pristine */
		config = kmalloc(config_sz, GFP_KERNEL);
		if (!config) {
			pr_emerg("%s: No memory to copy configuration\n",
				 __func__);
			return -ENOMEM;
		}
		memcpy(config, config_base, config_sz);
		data = config;

		/* Find end of configuration data and clean it up. */
		while (!(data[0] == 0 && data[1] == 0)) {
			/* each entry is delimited by the BELL char */
			if (*data == '\x7') {
				*data = '\0';
				++ents;
			}
			++data;
		}
		++ents;
	}
	if (ents == 0) {
		pr_info("%s: Configuration data is either missing or empty\n",
			__func__);
		pr_info("%s: Continuing with personality\n", __func__);
		data = NULL;
	}

	end = data;
	ents += ARRAY_SIZE(bgq_defconfig);

	local_config = kzalloc(ents * sizeof(*local_config), GFP_KERNEL);
	if (!local_config) {
		pr_emerg("%s: unable to allocate config table\n", __func__);
		kfree(config);
		return -ENOMEM;
	}

	/* copy in the known bgq_defs */
	memcpy(local_config, bgq_defconfig, sizeof(bgq_defconfig));

	/* fill in any user added configs passed the bgq_defs */
	for (i = ARRAY_SIZE(bgq_defconfig); i < ents; i++)
		local_config[i].value = bgq_def_empty;

	/*
	 * Make second pass through config and insert the values into
	 * our new table.  If there is nothing to do then end will be 0.
	 */
	var_name = config;
	while (var_name < end) {
		char *eq;

		/* get the variable name */
		eq = strchr(var_name, '=');
		if (eq) {
			*eq = '\0';
			++eq;
		} else {
			pr_info("%s: giving up configuration processing.\n",
				__func__);
			pr_info("%s: lost '=' at 0x%lx\n", __func__,
				var_name - config);
			var_name = end;
			continue;
		}
		bgq_add_config(local_config, ents, var_name, eq);
		var_name = eq + strlen(eq) + 1;
	}

	rc = bgq_pers_make_list(ents, local_config, &pers);
	if (rc) {
		pr_emerg("%s: failed to make list at %u\n", __func__, rc);
		rc = -ENOMEM;
	} else {
		/* don't need this anymore */
		kfree(local_config);
		local_config = NULL;

		if (!misc_register(&bgq_persdev))
			return 0;

		pr_emerg("%s: failed to register\n", __func__);
		bgq_pers_free_list();
		rc = -ENODEV;
	}
	kfree(config);
	kfree(local_config);

	return rc;
}

static void __exit bgq_persdev_module_exit(void)
{
	printk(KERN_INFO "Releasing the Blue Gene/Q Personality Device\n");

	misc_deregister(&bgq_persdev);
	bgq_pers_free_list();
}

device_initcall(bgq_persdev_module_init);
module_exit(bgq_persdev_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jay S. Bryant <jsbryant@us.ibm.com>");
MODULE_AUTHOR("Jimi Xenidis <jimix@pobox.com>");
MODULE_DESCRIPTION("IBM Blue Gene/Q Personality Device Driver");
