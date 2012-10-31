/*
 * Blue Gene/Q boot wrapper
 *
 * Copyright 2012 IBM Corporation.
 *   Jimi Xenidis <jimix@pobox.com>
 *
 * Copyright 2010 IBM Corporation.
 *   Andrew Tauferner <ataufer@us.ibm.com>
 *   Todd Inglett <tinglett@us.ibm.com>
 *
 * Based on earlier code:
 *   Copyright 2007 David Gibson, IBM Corporation.
 *   Copyright (C) Paul Mackerras 1997.
 *
 *   Matt Porter <mporter@kernel.crashing.org>
 *   Copyright 2002-2005 MontaVista Software Inc.
 *
 *   Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 *   Copyright (c) 2003, 2004 Zultys Technologies
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include "types.h"
#include "ops.h"
#include "stdio.h"
#include "reg.h"
#include "io.h"

#include "../platforms/bgq/personality.h"

/* Core Configuration Register 2 */
#define SPRN_A2_CCR2			0x3f2
#define A2_CCR2_ERAT_ONLY_MODE		0x00000001

/* declare a 4K stack so that we don't use Firmware's */
BSS_STACK(4096);

static char kernel_cmd_line[COMMAND_LINE_SIZE];

/* this is going to be a problem */
#define BGQ_RAMDISK_ADDR 0x1000000UL

/*
 * The Firmware Interface is defined in terms of the 64bit ABI,
 * however we are and must remain 32bit, so this is a special version
 * just for this boot phase
 */
#define BGQ_FIRMWARE_VERSION 0x00000001
struct bgq_fw_iface {
	u32 crc;
	u32 version;
	u64 vectors[0];
};

enum bgq_fw_vectors {
	bfv_exit = 2,
	bfv_terminate = 3,
	bfv_get_personality = 6,
	bfv_write_ras_event = 7,
	bfv_write_ras_string = 8,
	bfv_putn = 20,
	bfv_is_io_node = 21,
	bfv_take_cpu = 22,
	bfv_get_domain_descriptor = 26,
	bfv_send_block_status = 31,
	bfv_poll_inbox = 32,
	bfv_flush_ras_buffers = 33,
};

#define FW_OK 0
#define FW_ERROR -1

/* This is a local copy we made in bgq-head.S */
static u32 bgq_fw_hi;
static u32 bgq_fw_lo;

static int bgq_io_node;

/* these is defined in bgq-head.S */
/*
 * yes, I know these should be in a header, but there isn't a good
 * candidate and it _is_ boot code
 */
extern void bgq_kexec_primary_thread(unsigned long r3);
extern void bgq_kexec_secondary_thread(unsigned long r3);
extern void bgq_kexec_thread_0_1(unsigned long r3);
extern int bgq_fw_call(unsigned long r3, unsigned long r4, unsigned long r5,
		       unsigned long r6, unsigned long offset,
		       u32 fw_hi, u32 fw_lo);

static int bgq_fw_call_off(u32 r3, u32 r4, u32 r5, u32 r6, u32 offset)
{
	return bgq_fw_call(r3, r4, r5, r6, offset, bgq_fw_hi, bgq_fw_lo);
}

static void bgq_exit(void)
{
	u32 off = offsetof(struct bgq_fw_iface, vectors[bfv_exit]);

	bgq_fw_call_off(1, 0, 0, 0, off);
}

static void bgq_console_write(const char *s, int length)
{
	u32 off = offsetof(struct bgq_fw_iface, vectors[bfv_putn]);

	bgq_fw_call_off((unsigned long)s, length, 0, 0, off);
}

static int bgq_get_personality(void *buf, unsigned sz)
{
	int rc;
	u32 off = offsetof(struct bgq_fw_iface, vectors[bfv_get_personality]);

	rc = bgq_fw_call_off((unsigned long)buf, sz, 0, 0, off);
	if (rc != FW_OK) {
		fatal("unable to get BGQ personality");
		return -1;
	}
	return 0;
}

static int bgq_get_domain_descriptor(void *buf)
{
	int rc;
	u32 off;

	off = offsetof(struct bgq_fw_iface,
		       vectors[bfv_get_domain_descriptor]);

	rc = bgq_fw_call_off((unsigned long)buf, 0, 0, 0, off);
	if (rc) {
		fatal("unable to get BGQ domain descriptor");
		return -1;
	}
	return 0;
}

static int bgq_is_io_node(void)
{
	u32 off;

	off = offsetof(struct bgq_fw_iface, vectors[bfv_is_io_node]);

	if (bgq_fw_call_off(0, 0, 0, 0, off))
		return 1;
	return 0;
}

static int bgq_take_cpu(unsigned thread, void *entry, unsigned long gpr3)
{
	int rc;
	u32 off;
	unsigned thread_mask;
	unsigned core;

	off = offsetof(struct bgq_fw_iface, vectors[bfv_take_cpu]);

	core = thread >> 2;
	thread_mask = 1U << (thread & 0x3);

	rc = bgq_fw_call_off(core, thread_mask, (unsigned long)entry,
			     gpr3, off);

	if (rc) {
		fatal("unable to start thread: %u\n", thread);
		return -1;
	}
	return 0;
}

static int bgq_send_block_status(u16 status)
{
	u32 off;

	off = offsetof(struct bgq_fw_iface,
		       vectors[bfv_send_block_status]);

	return bgq_fw_call_off(status, 0, 0, 0, off);
}

/**
 * struct bgq_fw_domain_descriptor - base domain configuration
 * @core_mask:		LSB indicates core 0 is active
 * @ddr_origin:		Base (physical address)
 * @ddr_end:		End of domain's (phyiscal) memory
 * @entry_point:	Entry point of domain
 * @config_address:	Address of domain's config area
 * @config_length:	Length of domain's config area
 * @options:		Domain's command line options
 *
 * The BGQ firmware has the ability to split the cores of a node
 * into multiple domains with dedicated memory. This structure is
 * used to describe each domain.
 *
 */
struct bgq_fw_domain_descriptor {
	u32 core_mask;
	u32 _pad;
	u64 ddr_origin;
	u64 ddr_end;
	u64 entry_point;
	u64 config_address;
	u32 config_length;
	char options[512];
};

/* Set initrd & bootargs information in device tree. */
static int bgq_fixup_chosen(const char *options, u32 rd_addr)
{
	void *node;
	int rc;
	u32 *rd;

	node = finddevice("/chosen");

	if (!node) {
		fatal("could not find chosen node in device tree");
		return -1;
	}

	/*
	 * On Blue Gene we may have a ramdisk loaded at a fixed
	 * address (0x1000000).	 It is preceeded by a 4-byte magic value
	 * and a 4-byte big endian length.
	 */
	rd = (u32 *)rd_addr;

	if (rd[0] == 0xf0e1d2c3UL && rd[1] > 0) {
		u32 initrd_start = rd_addr + 8;
		u32 initrd_end = initrd_start + rd[1];

		setprop_val(node, "linux,initrd-start", initrd_start);
		setprop_val(node, "linux,initrd-end", initrd_end);
	}

	/* If a kernel command line is specified then use that. */
	if (options[0] == 0)
		return 0;

	if (strlen(options) >= sizeof(kernel_cmd_line)) {
		fatal("Domain->options too big: %s", options);
		return -1;
	}

	/* Update kernel bootargs */
	rc = getprop(node, "bootargs", kernel_cmd_line,
		     sizeof(kernel_cmd_line));
	if (rc <= 0) {
		fatal("Missing bootargs in kernel device tree.\n");
		return -1;
	}
	strcpy(kernel_cmd_line, options);

	kernel_cmd_line[rc] = '\0';
	rc = setprop_str(node, "bootargs", kernel_cmd_line);
	if (rc) {
		fatal("Failure updating bootargs, rc=%d\n", rc);
		return -1;
	}
	return 0;
}

/*
 * Delete any CPUs from the device tree that aren't allowed by the
 * specified core mask.
 */
static int bgq_fixup_cpus(u32 core_mask)
{
	int i;
	u32 mask = core_mask;
	void *devp;
	void *lastNode = NULL;
	char *core_str = "00000000000000000";
	u32 num_cores = 0;

	for (i = 0, mask <<= 15; mask; mask <<= 1, i++) {
		if (mask & 0x80000000) {
			num_cores++;
			core_str[i] = '1';
		}
	}

	printf("CPU cores <- %sb (%u cores)\n\r", core_str, num_cores);

	while ((devp = find_node_by_devtype(lastNode, "cpu"))) {
		u32 reg;

		if (getprop(devp, "reg", &reg, sizeof(reg)) == sizeof(reg)) {
			u32 core = reg / 4;

			if (!((1 << core) & core_mask))
				del_node(devp);
			else
				lastNode = devp;
		}
	}
	return num_cores;
}

#define BGQ_PERS_ENABLE_PCIE	0x20ULL
static void bgq_fixup_pcie(u64 node_config)
{
	void *devp;

	/* if we are an IO Node, then leave the tree as is */
	if (node_config & BGQ_PERS_ENABLE_PCIE)
		return;

	/* if we are not an IO Node, then prune the PCIE node */
	devp = find_node_by_devtype(NULL, "pciex");
	if (devp)
		del_node(devp);
}

static void bgq_fixup_bgq_soc(struct bgq_fw_domain_descriptor *bgd,
			      struct bgq_personality *bgp)
{
	void *node;
	static const char const *soc = "ibm,bgq-soc";
	u64 size;

	node = find_node_by_compatible(NULL, soc);
	if (!node) {
		fatal("No BG/Q node of type: %s\n", soc);
		return;
	}

	/* simply stash this as a property */
	setprop(node, "ibm,bgq-personality", bgp, sizeof(*bgp));

	/*
	 * We cannot reach this memory in 32bits, so we note where it
	 * is and the kernel will reserve the area early.
	 */
	setprop_val(node, "ibm,bgq-config-start", bgd->config_address);
	size = bgd->config_length;
	setprop_val(node, "ibm,bgq-config-size", size);
	printf("BGQ Config: 0x%llx:0x%llx\n", bgd->config_address, size);
}

/* Updates for BGQ. */
static void bgq_fixups(void)
{
	u64 ddr_size;
	unsigned int freq;
	struct bgq_personality bgp;
	struct bgq_fw_domain_descriptor bgd;
	u32 ccr2;

	if (bgq_get_personality(&bgp, sizeof(bgp))) {
		fatal("Could not get BGQ personality");
		return;
	}

	if (bgp.version != BGQ_PERSONALITY_VERSION) {
		fatal("wrong BGQ personality version: %u != %u",
		      bgp.version, BGQ_PERSONALITY_VERSION);
		return;
	}

	if (bgq_get_domain_descriptor(&bgd)) {
		fatal("Could not get BGQ domain descriptor");
		return;
	}

	/* Validate configuration information. */
	if (bgd.config_length == 0) {
		fatal("BGQ: config_length is 0");
		return;
	}

	if (bgd.config_address == 0) {
		fatal("BGQ: config_address is 0");
		return;
	}

	if (bgd.config_address + bgd.config_length > bgd.ddr_end) {
		fatal("Configuration data does not fit in available memory\n");
		return;
	}

	ddr_size = bgd.ddr_end + 1;
	ddr_size -= bgd.ddr_origin;
	dt_fixup_memory(bgd.ddr_origin, ddr_size);

	/* Fix clock rate */
	freq = bgp.kernel_config.freq_mhz * 1000000UL;
	dt_fixup_cpu_clocks(freq, freq, freq);

	/* fixup the rest */
	bgq_fixup_bgq_soc(&bgd, &bgp);
	bgq_fixup_chosen(bgd.options, BGQ_RAMDISK_ADDR);
	bgq_fixup_cpus(bgd.core_mask);
	bgq_fixup_pcie(bgp.kernel_config.node_config);

	/* make sure that the boot CPU has the TLB turned on */
	ccr2 = mfspr(SPRN_A2_CCR2);
	ccr2 &= ~A2_CCR2_ERAT_ONLY_MODE;
	mtspr(SPRN_A2_CCR2, ccr2);
}

static void bgq_take_and_ack(unsigned t, void *fd)
{
	/* wait for __secondary_hold_acknowledge to become t */
	u64 *ack = (u64 *)0x10UL;
	int rc;

	rc = bgq_take_cpu(t, fd, t);
	if (rc)
		fatal("failed to take thread %d\n", t);

	while (*ack != t) {
		/* need to add a timeout that calls fatal */
		barrier();
	}
}

static void bgq_kexec_threads(void *addr, unsigned long size)
{
	void *devp = NULL;
	u64 fd_pri[3] = { [0] = (unsigned long)bgq_kexec_primary_thread };
	u64 fd_sec[3] = { [0] = (unsigned long)bgq_kexec_secondary_thread };
	u64 fd_01[3] = { [0] = (unsigned long)bgq_kexec_thread_0_1 };
	unsigned tsum = 0;

	printf("kernel is here %p and %lu big\n", addr, size);

	/* copy the kexec area down to 0 */
	if (addr) {
		memcpy(NULL, addr, 0x100);
		flush_cache(NULL, 0x100);
	}

	while ((devp = find_node_by_devtype(devp, "cpu"))) {
		u32 threads[4];
		int rc;
		int t;
		int i = 0;

		rc = getprop(devp, "ibm,ppc-interrupt-server#s",
			     &threads, sizeof(threads));

		if (rc <= 0)
			fatal("No threads");

		setprop_str(devp, "enable-method", "kexec");

		t = rc / sizeof(threads[0]);


		/*
		 * we assume that we are booting on the 0th thread,
		 * and with the BGQ loader that is a safe
		 * assumption
		 */
		i = 0;
		if (threads[i] == 0) {
			/* skip to next thread */
			++i;
			/*
			 * second thread of booting core needs special
			 * handling
			 */
			bgq_take_and_ack(threads[i], fd_01);
			++tsum;
			/* skip to next cpu */
			++i;
		}

		/* take all the secondary threads to the kernels kexec spin */
		while (i < t) {
			if (i == 0)
				bgq_take_and_ack(threads[i], fd_pri);
			else
				bgq_take_and_ack(threads[i], fd_sec);
			++tsum;
			++i;
		}
	}
	printf("All %u secondary threads in kexec\n", tsum);

	/* tell the BG/Q manager that we are fine */
	bgq_send_block_status(1);
}

/*
 * bgq-head.S has arranged for the following register contents
 *  r3: FW info 64-bit address hi word
 *  r4: FW info 64-bit address lo word
 *  r5: FW Version
 *  r6: FW CRC value
 */
void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
		   unsigned long r6, unsigned long r7)
{
	static const char * const node_type[] = { "Compute", "I/O" };

	/* BGQ version was passed in uing R5 */
	if (r5 != BGQ_FIRMWARE_VERSION) {
		for (;;)
			continue;
	}

	/* 64-bit firmware actual address is in GPR3 and GPR4 */
	bgq_fw_hi = r3;
	bgq_fw_lo = r4;

	/* Enable writing to the console. */
	console_ops.write = bgq_console_write;

	bgq_io_node = bgq_is_io_node();

	printf("\n-- Blue Gene/Q %s Node boot wrapper --\n",
	       node_type[bgq_io_node]);

	printf("Using firmware info table located at 0x%08x%08x\n",
	       bgq_fw_hi, bgq_fw_lo);

	/* Initialize memory allocation. */
	simple_alloc_init(_end, 256 << 20, 32, 128);

	/* Define the platform ops. */
	platform_ops.fixups = bgq_fixups;
	platform_ops.exit = bgq_exit;
	platform_ops.spin_threads = bgq_kexec_threads;

	/* Start device tree initialization. */
	fdt_init(_dtb_start);
}
