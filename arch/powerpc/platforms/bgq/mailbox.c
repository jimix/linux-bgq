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
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/memblock.h>
#include <linux/circ_buf.h>

#include <asm/dcr.h>
#include <asm/udbg.h>

#include "bgq.h"
#include "bic.h"

#define BGQ_DCR_TI_INTERRUPT_STATE 0x10
#define BGQ_DCR_TI_INTERRUPT_STATE_CRTL_HI 0x12
#define BGQ_DCR_TI_MAILBOX_REG0 0x160
#define BGQ_DCR_TI_MAILBOX_REG(x) (BGQ_DCR_TI_MAILBOX_REG0 + ((x) * 0x10))
#define BGQ_DCR_TI_USERCODE0 0x200
#define BGQ_DCR_TI_USERCODE(x) (BGQ_DCR_TI_USERCODE0 + ((x) * 0x10))
#define BGQ_MB_OUT_WP BGQ_DCR_TI_MAILBOX_REG(0)
#define BGQ_MB_OUT_RP BGQ_DCR_TI_MAILBOX_REG(1)
#define BGQ_MB_IN_WP BGQ_DCR_TI_MAILBOX_REG(2)
#define BGQ_MB_IN_RP BGQ_DCR_TI_MAILBOX_REG(3)


struct bgq_mailbox {
	void __iomem *bedram;

	/* consider separating these spinlocks by the cache block value */
	void __iomem *inbox;
	u64 inbox_rp;
	u64 inbox_sz;
	/* we need to protect the access to the inbox registers */
	spinlock_t inbox_lock;

	void __iomem *outbox;
	u64 outbox_sz;
	u64 outbox_wp;
	/* we need to protect access to the outbox registers */
	spinlock_t outbox_lock;

	dcr_host_t dcr_ti;

	struct circ_buf putc;
	unsigned putc_sz;

	struct circ_buf getc;
	unsigned getc_sz;
};

static struct bgq_mailbox mbox;

struct bgq_mailbox_header {
	u16 cmd;
	u16 payload_len;
	u16 thread_id;		/* HW thread id of sender */
	u16 crc;		/* 0 means no crc */
	char data[0];
};

#define BGQ_OUTBOX_STDOUT	0x0002 /*  stdout message */
#define BGQ_OUTBOX_TERMINATE	0x0008 /*  termination Request */
#define BGQ_OUTBOX_RAS_ASCII	0x0080 /*  RAS message */
#define BGQ_OUTBOX_BLOCK_STATE	0x0400 /*  stderr message */
#define BGQ_OUTBOX_STDERR	0x0100 /*  stderr message */

void __init bgq_mailbox_init(void)
{
	struct device_node *dn;
	struct bgq_mailbox *bm = &mbox;
	unsigned dcr_base;
	unsigned dcr_len;
	u64 u;
	u32 off;

	dn = of_find_compatible_node(NULL, NULL, "ibm,bgq-mailbox");
	BUG_ON(!dn);

	/* get DCR information */
	dcr_base = dcr_resource_start(dn, 0);
	dcr_len = dcr_resource_len(dn, 0);
	/* note, we allow DCR base of 0, GEA is there */
	BUG_ON(dcr_len == 0);

	bm->dcr_ti = dcr_map(dn, dcr_base, dcr_len);
	BUG_ON(!DCR_MAP_OK(bm->dcr_ti));

	/* clear mbox interrupts */
	dcr_write64(bm->dcr_ti, BGQ_DCR_TI_INTERRUPT_STATE,
		    0xffffffff00000000ULL);

	/*
	 * The inbox and outbox have been configured to use BeDRAM, so
	 * lets find it and map it.
	 */
	dn = of_find_compatible_node(NULL, NULL, "ibm,bgq-bedram");
	BUG_ON(!dn);

	bm->bedram = of_iomap(dn, 0);
	BUG_ON(!bm->bedram);

	/* setup the outbox */
	u = dcr_read64(bm->dcr_ti, BGQ_DCR_TI_USERCODE(0));
	bm->outbox_sz = u >> 32;
	off = u & 0xffffffffULL;
	bm->outbox = bm->bedram + off;

	/* initialize the write pointer and lock */
	bm->outbox_wp = dcr_read64(bm->dcr_ti, BGQ_MB_OUT_WP);
	spin_lock_init(&bm->outbox_lock);

	/* set up the inbox */
	u = dcr_read64(bm->dcr_ti, BGQ_DCR_TI_USERCODE(1));
	bm->inbox_sz = u >> 32;
	off = u & 0xffffffffULL;
	bm->inbox = bm->bedram + off;

	/* initialize the read pointer and lock */
	bm->inbox_rp = dcr_read64(bm->dcr_ti, BGQ_MB_IN_RP);
	spin_lock_init(&bm->inbox_lock);
}


#define BGQ_INBOX_SYSREQ_SHUTDOWN		1
#define BGQ_INBOX_SYSREQ_SHUTDOWN_IO_LINK	2
static int bgq_inbox_sysreq(const void __iomem *srp, unsigned len)
{
	const u32 __iomem *sr = srp;
	u32 req_id;
	u32 block_id;

	BUG_ON(len != (sizeof(req_id) + sizeof(block_id)));

	/* this will never wrap in the ring queue */
	req_id = ACCESS_ONCE(sr[0]);

	switch (req_id) {
	case BGQ_INBOX_SYSREQ_SHUTDOWN:
		pr_emerg("%s: shutting down system now!\n", __func__);
		orderly_poweroff(true);
		bgq_panic("orderly_poweroff() returned");
		break;
	case BGQ_INBOX_SYSREQ_SHUTDOWN_IO_LINK:
		block_id = ACCESS_ONCE(sr[0]);
		pr_info("%s: need to shutdown block id: 0x%x\n", __func__,
			block_id);
		return 0;
	}
	pr_err("%s: Unhandled sysreq: 0x%x\n", __func__, req_id);
	return -1;
}

static int bgq_inbox_get_chars(void *buf, unsigned len)
{
	struct bgq_mailbox *bm = &mbox;
	ulong head;
	ulong tail;
	ulong flags;
	unsigned sz;

	local_irq_save(flags);

	head = ACCESS_ONCE(bm->getc.head);
	tail = bm->getc.tail;

	sz = CIRC_CNT(head, tail, bm->getc_sz);

	if (sz > 0) {
		if (len < sz)
			sz = len;
		/* read index before reading contents at that index */
		smp_read_barrier_depends();
		if (tail + sz > bm->getc_sz) {
			unsigned space = bm->getc_sz - tail;

			if (sz - space > bm->getc_sz)
				bgq_panic("buffer too big\n");
			memcpy(buf, &bm->getc.buf[tail], space);
			memcpy(buf + space, &bm->getc.buf[0], sz - space);
		} else {
			memcpy(buf, &bm->getc.buf[tail], sz);
		}
		 /* finish reading descriptor before incrementing tail */
		smp_mb();
		bm->getc.tail = (tail + sz) & (bm->getc_sz - 1);
	}

	local_irq_restore(flags);

	return sz;
}

static int bgq_inbox_stdin(const void __iomem *srp)
{
	struct bgq_mailbox *bm = &mbox;
	const char __iomem *str;
	ulong head;
	ulong tail;
	ulong flags;
	unsigned sz;
	int i;
	int count;
	int c;


	count = ACCESS_ONCE(*(u16 *)srp);
	str = srp + sizeof(u16);

	/* really dumb for now */
	for (i = 0; i < count; i++) {
		c = *str;

		local_irq_save(flags);
		head = bm->getc.head;
		tail = ACCESS_ONCE(bm->getc.tail);
		sz = CIRC_SPACE(head, tail, bm->getc_sz);
		BUG_ON(sz == 0);

		bm->getc.buf[head] = c;

		/* commit the item before incrementing the head */
		smp_wmb();
		bm->getc.head = (head + 1) & (bm->getc_sz - 1);
		local_irq_restore(flags);

		str++;
		/* this could wrap */
		if ((ulong)str > ((ulong)bm->inbox + bm->inbox_sz))
			str = bm->inbox;
	}

	return 1;
}

/*
 * == 0 means success but no console input
 * == 1 success with console input, and how many chars
 * < 0 error
 */
#define BGQ_INBOX_CMD_NONE	0x0
#define BGQ_INBOX_CMD_STDIN	0x5
#define BGQ_INBOX_CMD_SYSREQ	0x9
static int bgq_mailbox_in(void)
{
	struct bgq_mailbox *bm = &mbox;
	struct bgq_mailbox_header __iomem *mp;
	u64 wp;
	u64 rp;
	ulong flags;
	int rc = 0;
	u16 cmd;
	u16 len;
	unsigned roff;

	if (!bm->outbox)
		return -ENODEV;

	/*
	 * Apparently it could take the control system up to 3 minutes
	 * (yes _3_) to have a message ready for us. Obviously, we
	 * cannot wait what long becuase we may be in interrupt
	 * context, so we will depend on the HVC console's thread to
	 * pick at us at a resonable interval.  If this is not
	 * suffucient, then we should consider setting up our own
	 * kthread for polling.
	 */
	spin_lock_irqsave(&bm->inbox_lock, flags);

	rp = bm->inbox_rp;
	for (;;) {
		wp = dcr_read64(bm->dcr_ti, BGQ_MB_IN_WP);

		if (wp == rp)
			break;

		roff = rp & (bm->inbox_sz - 1);

		mp = bm->inbox + roff;
		/* grab a local copy  of the values */
		cmd = ACCESS_ONCE(mp->cmd);
		len = ACCESS_ONCE(mp->payload_len);
		switch (cmd) {
		default:
			pr_warn("%s: unknown command: 0x%x\n", __func__, cmd);
			break;
		case BGQ_INBOX_CMD_NONE:
			break;
		case BGQ_INBOX_CMD_STDIN:
			rc = bgq_inbox_stdin(mp->data);
			break;
		case BGQ_INBOX_CMD_SYSREQ:
			bgq_inbox_sysreq(mp->data, len);
			break;
		}
		len += sizeof(*mp);
		len = round_up(len, 0x10);
		rp += len;

		bm->inbox_rp = rp;
		dcr_write64(bm->dcr_ti, BGQ_MB_IN_RP, rp);
		if (rc < 0)
			break;
	}
	spin_unlock_irqrestore(&bm->inbox_lock, flags);

	return rc;
}

static int bgq_mailbox_out(u16 cmd, const void *buf, u16 sz)
{
	struct bgq_mailbox *bm = &mbox;
	struct bgq_mailbox_header __iomem *mp;
	u64 rp;
	u64 wp;
	ulong flags;
	ulong len;
	unsigned space;
	unsigned woff;

	if (!bm->outbox)
		return -ENODEV;

	if (sz == 0)
		return 0;

	len = sizeof(*mp) + sz;
	/* commands are always aligned to 16 bytes */
	len = round_up(len, 0x10);

	/*
	 * we need to preserve the order of messages so there is no
	 * need to do anything fancy to let another message that fits
	 * through
	 */
	spin_lock_irqsave(&bm->outbox_lock, flags);

	wp = bm->outbox_wp;
	for (;;) {
		rp = dcr_read64(bm->dcr_ti, BGQ_MB_OUT_RP);

		space = wp + len - rp;
		if (space > bm->outbox_sz) {
			/*
			 * wait for control system to catch up
			 * can't sleep because we may be in interrupt
			 */
			udelay(10);
			continue;
		}
		break;
	}

	woff = wp & (bm->outbox_sz - 1);
	mp = bm->outbox + woff;

	/*
	 * since mp is always aligned at 16 bytes we can freely write
	 * the header
	 */
	mp->cmd = cmd;
	mp->payload_len = sz;
	mp->thread_id = hard_smp_processor_id();
	mp->crc = 0;

	woff += sizeof(*mp);

	if (woff + sz > bm->outbox_sz) {
		space = bm->outbox_sz - woff;
		memcpy(mp->data, buf, space);
		memcpy(bm->outbox, buf + space, sz - space);
	} else {
		memcpy(mp->data, buf, sz);
	}

	iobarrier_w();

	wp += len;

	bm->outbox_wp = wp;
	dcr_write64(bm->dcr_ti, BGQ_MB_OUT_WP, wp);

	spin_unlock_irqrestore(&bm->outbox_lock, flags);

	return sz;
}

static void bgq_outbox_terminate(u32 status)
{
	struct term {
		u64 timebase;
		u32 status;
		u32 _tail_pad;
	} t;
	t.timebase = get_tb();
	t.status = status;
	bgq_mailbox_out(BGQ_OUTBOX_TERMINATE, &t,
			sizeof(t) - sizeof(t._tail_pad));
}

void bgq_halt(void)
{
	bgq_mailbox_out(BGQ_OUTBOX_STDOUT, __func__, sizeof(__func__) - 1);
	bgq_outbox_terminate(0);
	for (;;)
		continue;
}

void bgq_restart(char *s)
{
	bgq_mailbox_out(BGQ_OUTBOX_STDOUT, __func__, sizeof(__func__) - 1);
	bgq_mailbox_out(BGQ_OUTBOX_STDOUT, s, strlen(s));
	bgq_outbox_terminate(0);
	for (;;)
		continue;
}

#define	BGQ_OUTBOX_RAS_KERNEL_PANIC		 0xa000d

int bgq_ras_puts(u64 id, const char *s)
{
	struct ras {
		u64 uci;
		u32 id;
		char msg[4];	/* should be 0 but we claim the pad */
	} *r;
	unsigned l = strlen(s);
	unsigned sz = sizeof(*r) - sizeof(r->msg) + l;

	r = __builtin_alloca(sz);

	r->uci = 0;
	r->id = id;
	memcpy(r->msg, s, l);

	return bgq_mailbox_out(BGQ_OUTBOX_RAS_ASCII, r, sz);
}

void bgq_panic(char *s)
{
	bgq_mailbox_out(BGQ_OUTBOX_STDERR, __func__, sizeof(__func__) - 1);
	bgq_mailbox_out(BGQ_OUTBOX_STDERR, s, strlen(s));
	bgq_ras_puts(BGQ_OUTBOX_RAS_KERNEL_PANIC, s);
	bgq_outbox_terminate(-1);
	for (;;)
		continue;
}

int bgq_block_state(u16 status)
{
	struct sb {
		u16 status;
		u16 __unused;
		u32 block_id;
	};
	struct sb sb = { .status = status };

	return bgq_mailbox_out(BGQ_OUTBOX_BLOCK_STATE, &sb, sizeof(sb));
}

static void bgq_stdout_flush(void)
{
	struct bgq_mailbox *bm = &mbox;
	ulong head;
	ulong tail;
	ulong flags;
	unsigned sz;

	local_irq_save(flags);

	head = ACCESS_ONCE(bm->putc.head);
	tail = bm->putc.tail;

	sz = CIRC_CNT(head, tail, bm->putc_sz);
	/* read index before reading contents at that index */
	smp_read_barrier_depends();
	if (sz > 0) {
		if (tail + sz > bm->putc_sz) {
			unsigned space = bm->putc_sz - tail;
			char *tmp;

			if (sz - space > bm->putc_sz)
				bgq_panic("buffer too big\n");
			/*
			 * we copy it into a temp buffer so we can get
			 * it out all as one line
			 */
			tmp = __builtin_alloca(sz);
			memcpy(tmp, &bm->putc.buf[tail], space);
			memcpy(tmp + space, &bm->putc.buf[0], sz - space);

			bgq_mailbox_out(BGQ_OUTBOX_STDOUT, tmp, sz);
		} else {
			bgq_mailbox_out(BGQ_OUTBOX_STDOUT,
					&bm->putc.buf[tail], sz);
		}
		/* finish reading descriptor before incrementing tail */
		smp_mb();
		bm->putc.tail = (tail + sz) & (bm->putc_sz - 1);
	}

	local_irq_restore(flags);
}

static void bgq_stdout_putc(char c)
{
	struct bgq_mailbox *bm = &mbox;
	ulong head;
	ulong tail;
	ulong flags;
	unsigned sz;

	if (c == '\n')
		bgq_stdout_flush();

	local_irq_save(flags);

	for (;;) {
		head = bm->putc.head;
		tail = ACCESS_ONCE(bm->putc.tail);

		sz = CIRC_SPACE(head, tail, bm->putc_sz);
		if (sz > 0)
			break;

		bgq_stdout_flush();
	}

	bm->putc.buf[head] = c;
	/* commit the item before incrementing the head */
	smp_wmb();
	bm->putc.head = (head + 1) & (bm->putc_sz - 1);

	local_irq_restore(flags);
}

static int udbg_bgq_getc_poll(void)
{
	char c;
	int rc;

	rc = bgq_inbox_poll(0, &c, 1);
	if (rc <= 0)
		return -1;
	return c;
}

static int udbg_bgq_getc(void)
{
	int ch;

	for (;;) {
		ch = udbg_bgq_getc_poll();
		if (ch >= 0)
			return ch;
	}
}

void __init udbg_init_bgq_early(void)
{
	struct bgq_mailbox *bm = &mbox;

	/* should this be double? */
	bm->putc_sz = bm->outbox_sz;
	bm->putc.buf = __va(memblock_alloc(bm->putc_sz, 128));
	if (!bm->putc.buf)
		bgq_panic("Could not allocate putc buffer\n");

	udbg_putc = bgq_stdout_putc;
	udbg_flush = bgq_stdout_flush;

	/* inbox messages can be quite large */
	/* should this be double? */
	bm->getc_sz = bm->inbox_sz;
	bm->getc.buf = __va(memblock_alloc(bm->getc_sz, 128));
	if (!bm->getc.buf)
		bgq_panic("Could not allocate getc buffer\n");

	udbg_getc = udbg_bgq_getc;
	udbg_getc_poll = udbg_bgq_getc_poll;
}

/* for the hvc driver */
int bgq_put_chars(u32 vtermno, const char *data, int count)
{
	int i;

	/*
	 * the outbox does provide and "stderr" channel that we may
	 * desire to wire up to vtermno = 1, but for now we don't
	 * support it.
	 */
	if (vtermno > 0)
		return 0;

	for (i = 0; i < count; i++)
		bgq_stdout_putc(data[i]);

	return count;
}

int bgq_inbox_poll(u32 vtermno, char *buf, int count)
{
	int rc;

	if (vtermno > 0)
		return 0;

	/* try to load up more */
	bgq_mailbox_in();
	rc = bgq_inbox_get_chars(buf, count);

	return rc;
}

/* for interrupt control of the inbox */
void bgq_inbox_mask_irq(void)
{
	struct bgq_mailbox *bm = &mbox;
	u64 v;

	v = dcr_read64(bm->dcr_ti, BGQ_DCR_TI_INTERRUPT_STATE_CRTL_HI);
	v &= ~(1ULL);
	dcr_write64(bm->dcr_ti, BGQ_DCR_TI_INTERRUPT_STATE_CRTL_HI, v);
}

void bgq_inbox_unmask_irq(void)
{
	struct bgq_mailbox *bm = &mbox;
	u64 v;

	v = dcr_read64(bm->dcr_ti, BGQ_DCR_TI_INTERRUPT_STATE_CRTL_HI);
	v |= 1ULL;
	dcr_write64(bm->dcr_ti, BGQ_DCR_TI_INTERRUPT_STATE_CRTL_HI, v);
}

void bgq_inbox_eoi(void)
{
	struct bgq_mailbox *bm = &mbox;
	u64 v;

	v = dcr_read64(bm->dcr_ti, BGQ_DCR_TI_INTERRUPT_STATE);
	dcr_write64(bm->dcr_ti, BGQ_DCR_TI_INTERRUPT_STATE, v);
}
