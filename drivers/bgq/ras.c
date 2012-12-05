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
#include <linux/types.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#include <platforms/bgq/bgq.h>

#define BGQ_RASDEV_MINOR 112
/* old name for compat */
#define BGQ_RASDEV_NAME "bgras"

struct bgq_ras_ids {
	const char *name;
	unsigned value;
};

#define BGQ_RAS_ID_STR(n, v) { .name = #n, .value = v }
static struct bgq_ras_ids bgq_ras_ids[] = {
	BGQ_RAS_ID_STR(BGRAS_ID_NONE, 0xa0000),
	BGQ_RAS_ID_STR(BGRAS_ID_PCIE_UNSUPPORTED_ADAPTER, 0xa0001),
	BGQ_RAS_ID_STR(BGRAS_ID_PCIE_MISSING_ADAPTER_VPD, 0xa0002),
	BGQ_RAS_ID_STR(BGRAS_ID_BGSYS_MOUNT_FAILURE, 0xa0003),
	BGQ_RAS_ID_STR(BGRAS_ID_GPFS_START_FAILURE, 0xa0004),
	BGQ_RAS_ID_STR(BGRAS_ID_NO_NETWORK_INTERFACE_DEFINED, 0xa0005),
	BGQ_RAS_ID_STR(BGRAS_ID_SCRIPT_FAILURE, 0xa0006),
	BGQ_RAS_ID_STR(BGRAS_ID_BGQ_DISTRO_MISSING, 0xa0007),
	BGQ_RAS_ID_STR(BGRAS_ID_GPFS_INIT_FAILURE, 0xa0008),
	BGQ_RAS_ID_STR(BGRAS_ID_PCIE_LINK_DEGRADED, 0xa0009),
	BGQ_RAS_ID_STR(BGRAS_ID_NETWORK_CONFIG_FAILURE, 0xa000a),
	BGQ_RAS_ID_STR(BGRAS_ID_INT_VECTOR_FAILURE, 0xa000b),
	BGQ_RAS_ID_STR(BGRAS_ID_GPFS_HOSTNAME_FAILURE, 0xa000c),
	BGQ_RAS_ID_STR(BGRAS_ID_KERNEL_PANIC, 0xa000d),
	BGQ_RAS_ID_STR(BGRAS_ID_ETHERNET_LINK_TIMEOUT, 0xa000e),
	BGQ_RAS_ID_STR(BGRAS_ID_IB_LINK_TIMEOUT, 0xa000f),
	BGQ_RAS_ID_STR(BGRAS_ID_NODE_HEALTH_MONITOR_WARNING, 0xa0010),
	BGQ_RAS_ID_STR(BGRAS_ID_ETHERNET_LINK_LOST, 0xa0011),
	BGQ_RAS_ID_STR(BGRAS_ID_IB_LINK_LOST, 0xa0012),
	BGQ_RAS_ID_STR(BGRAS_ID_ROOT_FS_UNRESPONSIVE, 0xa0013),
};

/* Functions for seqential access of the bgras structure */
static void *bgq_rasdev_seq_start(struct seq_file *f, loff_t *pos)
{
	if (*pos < ARRAY_SIZE(bgq_ras_ids))
		return pos;
	return NULL;
}

static void *bgq_rasdev_seq_next(struct seq_file *f, void *v, loff_t *pos)
{
	(*pos)++;
	if (*pos < ARRAY_SIZE(bgq_ras_ids))
		return pos;
	return NULL;
}

static void bgq_rasdev_seq_stop(struct seq_file *f, void *v)
{
}

/*
 * When we are read iterate through the events and ids arrays printing
 * out the data in environment variable format.
 */
static int bgq_rasdev_seq_show(struct seq_file *f, void *v)
{
	loff_t *off = v;
	unsigned pos = *off;
	struct bgq_ras_ids *bi;

	bi = &bgq_ras_ids[pos];

	seq_printf(f, "%s=0x%04x\n", bi->name, bi->value);

	return 0;
}


/*
 * Take the structure written in and send it on to the kernel to be
 * sent on to firmware.
 */
static ssize_t bgq_rasdev_write(struct file *file, const char __user *in,
				size_t size, loff_t *loff)
{
	int rc;
	struct bgq_ras {
		u32 msg_id;
		u8 is_binary;
		u16 len;
		char msg[0];
	};
	struct bgq_ras ras;
	char *msg = NULL;

	/*
	 * Make sure the structure sent is large enough to have at
	 * least a header.
	 */
	if (size < sizeof(ras)) {
		pr_emerg("%s: Bad header.\n", __func__);
		return -EINVAL;
	}

	if (copy_from_user(&ras, in, size)) {
		pr_emerg("%s: Failure accessing BG/Q RAS header.\n", __func__);
		return -EFAULT;
	}
	if (ras.len > 0) {
		msg = kmalloc(ras.len, GFP_KERNEL);
		if (!msg)
			return -ENOMEM;
		if (copy_from_user(msg, in, ras.len)) {
			pr_emerg("%s: Failure accessing BG/Q RAS message.\n",
				 __func__);
			kfree(msg);
			return -EFAULT;
		}
	}
	if (ras.is_binary) {
		pr_emerg("%s: Binary messages not supported yet", __func__);
		rc = -EINVAL;
	} else {
		if (strnlen(msg, ras.len) + 1 != ras.len) {
			pr_emerg("%s: RAS message not a string\n", __func__);
			rc = -EINVAL;
		} else {
			rc = bgq_ras_puts(ras.msg_id, msg);
		}
	}
	if (rc < 0)
		return rc;

	kfree(msg);

	return size;
}

static const struct seq_operations bgq_rasdev_seq_ops = {
	.start = bgq_rasdev_seq_start,
	.next = bgq_rasdev_seq_next,
	.stop = bgq_rasdev_seq_stop,
	.show = bgq_rasdev_seq_show
};

/* Set-up the sequential access functionality */
static int bgq_rasdev_open(struct inode *inode, struct file *f)
{
	f->private_data = NULL;
	return seq_open(f, &bgq_rasdev_seq_ops);
}

static const struct file_operations bgq_rasdev_fops = {
	.owner = THIS_MODULE,
	.open = bgq_rasdev_open,
	.write = bgq_rasdev_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static struct miscdevice bgq_rasdev = {
	.minor = BGQ_RASDEV_MINOR,
	.name = BGQ_RASDEV_NAME,
	.fops = &bgq_rasdev_fops
};

static int __init bgq_rasdev_module_init(void)
{
	pr_info("Initializing Blue Gene/Q RAS Device\n");

	/* Register the bgpers misc device ... Ensure an
	   rc of zero, return an error otherwise. */
	if ((misc_register(&bgq_rasdev)) != 0)
		return -ENODEV;

	return 0;
}

static void __exit bgq_rasdev_module_exit(void)
{
	pr_info("Releasing the Blue Gene/Q RAS Device\n");
	misc_deregister(&bgq_rasdev);
}

device_initcall(bgq_rasdev_module_init);
module_exit(bgq_rasdev_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jay S. Bryant <jsbryant@us.ibm.com>");
MODULE_AUTHOR("Jimi Xenidis <jimix@pobox.com>");
MODULE_DESCRIPTION("IBM Blue Gene/Q RAS device driver");
