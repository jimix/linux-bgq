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

/* bit codes for kernel.node_config */
#define FW_BIT(b) (1ULL << (63 - (b)))
#define PERS_ENABLE_MMU			FW_BIT(0)
#define PERS_ENABLE_IsIoNode		FW_BIT(1)
#define PERS_ENABLE_TakeCPU		FW_BIT(2)
#define PERS_ENABLE_MU			FW_BIT(3)
#define PERS_ENABLE_ND			FW_BIT(4)
#define PERS_ENABLE_Timestamps		FW_BIT(5)
#define PERS_ENABLE_BeDRAM		FW_BIT(6)
#define PERS_ENABLE_ClockStop		FW_BIT(7)
#define PERS_ENABLE_DrArbiter		FW_BIT(8)
#define PERS_ENABLE_DevBus		FW_BIT(9)
#define PERS_ENABLE_L1P			FW_BIT(10)
#define PERS_ENABLE_L2			FW_BIT(11)
#define PERS_ENABLE_MSGC		FW_BIT(12)
#define PERS_ENABLE_TestInt		FW_BIT(13)
#define PERS_ENABLE_NodeRepro		FW_BIT(14)
#define PERS_ENABLE_PartitionRepro	FW_BIT(15)
#define PERS_ENABLE_DD1_Workarounds	FW_BIT(16)
#define PERS_ENABLE_A2_Errata		FW_BIT(17)
#define PERS_ENABLE_A2_IU_LLB		FW_BIT(18)
#define PERS_ENABLE_A2_XU_LLB		FW_BIT(19)
#define PERS_ENABLE_DDRCalibration	FW_BIT(20)
#define PERS_ENABLE_DDRFastInit		FW_BIT(21)
#define PERS_ENABLE_DDRCellTest		FW_BIT(22)
#define PERS_ENABLE_DDRAutoSize		FW_BIT(23)
#define PERS_ENABLE_MaskLinkErrors	FW_BIT(24)
#define PERS_ENABLE_MaskCorrectables	FW_BIT(25)
#define PERS_ENABLE_DDRDynamicRecal	FW_BIT(26)
#define PERS_ENABLE_AppPreload		FW_BIT(36)
#define PERS_ENABLE_IOServices		FW_BIT(37)
#define PERS_ENABLE_SpecCapDDR		FW_BIT(38)
#define PERS_ENABLE_L2Only		FW_BIT(39)
#define PERS_ENABLE_FPGA		FW_BIT(40)
#define PERS_ENABLE_DDRINIT		FW_BIT(41)
#define PERS_ENABLE_Mailbox		FW_BIT(42)
#define PERS_ENABLE_Simulation		FW_BIT(43)
#define PERS_ENABLE_Mambo		FW_BIT(44)
#define PERS_ENABLE_JTagConsole		FW_BIT(45)
#define PERS_ENABLE_JTagLoader		FW_BIT(46)
#define PERS_ENABLE_FPU			FW_BIT(47)
#define PERS_ENABLE_L2Counters		FW_BIT(48)
#define PERS_ENABLE_Wakeup		FW_BIT(49)
#define PERS_ENABLE_BIC			FW_BIT(50)
#define PERS_ENABLE_DDR			FW_BIT(51)
#define PERS_ENABLE_GlobalInts		FW_BIT(54)
#define PERS_ENABLE_SerDes		FW_BIT(55)
#define PERS_ENABLE_UPC			FW_BIT(56)
#define PERS_ENABLE_EnvMon		FW_BIT(57)
#define PERS_ENABLE_PCIe		FW_BIT(58)
#define PERS_ENABLE_TimeSync		FW_BIT(61)
#define PERS_ENABLE_DiagnosticsMode	FW_BIT(63)

/* netflags2 */
#define TI_USE_PORT6_FOR_IO		FW_BIT(22)
#define TI_USE_PORT7_FOR_IO		FW_BIT(23)
#define ND_CN_BRIDGE_PORT_6		FW_BIT(29)
#define ND_CN_BRIDGE_PORT_7		FW_BIT(30)
#define ND_CN_BRIDGE_PORT_10		FW_BIT(31)

#endif	/* __BGQ_PERSONALITY_H_ */
