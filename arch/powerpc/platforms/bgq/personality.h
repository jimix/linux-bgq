/*
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
 * This software is available to you under either the GNU General
 * Public License (GPL) version 2 or the Eclipse Public License
 * (EPL) at your discretion.
 */

#ifndef __BGQ_PERSONALITY_H_
#define __BGQ_PERSONALITY_H_

/**
 * struct bgq_personality_kernel - kernel portions of BG/Q personality
 * @uci: Universal Component Identifier
 * @node_config: Kernel device and core enables
 * @trace_config: Kernel trace enables
 * @ras_policy: Verbosity level and RAS reporting
 * @freq_mhz: Clock frequence in MegaHertz
 * @clock_stop: Clockstop value
 *
 */
struct bgq_personality_kernel {
	u64 uci;
	u64 node_config;
	u64 trace_config;
	u32 ras_policy;
	u32 freq_mhz;
	u64 clock_stop;
};


/**
 * struct bgq_personality_ddr - DDR configuration portion of BG/Q personality
 * @ddr_flags: Miscellaneous flags and settings
 * @ddr_size_mb: Total DDR size in megabytes
 *
 *  DDRFlags is as follows:
 *
 *   +---------------------+----+
 *   |	reserved (unused)  | PD |
 *   +---------------------+----+
 *   0			29  30 31
 *
 *   PD - DDR Power Down mode
 *
 */
struct bgq_personality_ddr {
	u32 ddr_flags;
	u32 ddr_size_mb;
};

/**
 * struct bgq_personality_networks - network portion of BG/Q Personality
 * @block_id: aka partition ID
 * @net_flags: Network flags
 * @net_flags2: Network flags part 2
 * @a_nodes: A dimension (5 dimensional torus)
 * @b_nodes: B dimension
 * @c_nodes: C dimension
 * @d_nodes: D dimension
 * @e_nodes: E dimension
 * @a_coord: A coordinates
 * @b_coord: B cooridnates
 * @c_coord: C coordinates
 * @d_coord: D coordinates
 * @e_coord: E coordinates
 * &primordial_class_route: class routing configuration
 * @zone_routing_masks: each contains 5 masks
 * @mu_flags: Message Unit flags
 * @cn_bridge_a: Torus coordinates of compute node bridge
 * @cn_bridge_b: (you get the point)
 * @cn_bridge_c: (you get the point)
 * @cn_bridge_d: (you get the point)
 * @cn_bridge_e: (you get the point)
 * @latency_from_root: GI Latency from root node in pcklks
 *
 */
#define NUM_ND_ZONES 4
struct bgq_personality_networks {
	u32 block_id;
	u64 net_flags;
	u64 net_flags2;
	u8 a_nodes;
	u8 b_nodes;
	u8 c_nodes;
	u8 d_nodes;
	u8 e_nodes;
	u8 a_coord;
	u8 b_coord;
	u8 c_coord;
	u8 d_coord;
	u8 e_coord;

	struct bgq_primordial_class_route {
		u16 glob_int_up_port_inputs;
		u16 glob_int_up_port_outputs;
		u16 collective_type_and_up_port_inputs;
		u16 collective_up_port_outputs;
	} primordial_class_route;

	u32 zone_routing_masks[NUM_ND_ZONES];
	u64 mu_flags;
	u8 cn_bridge_a;
	u8 cn_bridge_b;
	u8 cn_bridge_c;
	u8 cn_bridge_d;
	u8 cn_bridge_e;
	u32 latency_from_root;
};

#define PERSONALITY_LEN_SECKEY 32
struct bgq_personality_ethernet {
	u8 security_key[PERSONALITY_LEN_SECKEY];
};


/**
 * struct bgq_personality - BG/Q Personality Structure
 * @crc: Crc16n starting from Version
 * @version: Personality version
 * @personality_size_words: sizeof(Personality_t) >> 2
 * &kernel_config: kernel configuration
 * &ddr_config: memory configuration
 * &network_config: network configuration
 * &ethernet_config: ethernet configuration
 *
 */
#define BGQ_PERSONALITY_VERSION 0x08
struct bgq_personality {
	u16 crc;
	u8 version;
	u8 personality_size_words;
	struct bgq_personality_kernel kernel_config;
	struct bgq_personality_ddr ddr_config;
	struct bgq_personality_networks network_config;
	struct bgq_personality_ethernet ethernet_config;
};

#endif	/* __BGQ_PERSONALITY_H_ */
