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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#include <platforms/bgq/bgq.h>

#define BGQ_BS_MINOR 113
/* we use the old name for backwards compatibility, change this later */
#define BGQ_BS_NAME "bgfirmware"
#define BGQ_BS_INITIALIZED 1
#define BGQ_BS_HALTED 2

static ssize_t bgq_bs_write(struct file *file, const char __user *in,
			     size_t size, loff_t *loff)
{
	int rc = 0;
	u16 status;
	char statusb[2];

	/* Should only be getting 1 character plus a newline right now. */
	if (size != sizeof(statusb))
		return -EINVAL;

	if (copy_from_user(statusb, in, size))
		return -EFAULT;

	/*
	 * At this point we should only be getting 1 or 2.  Return
	 * -EINVAL if we are passed anything else.
	 */
	if (statusb[1] != '\0')
		return -EINVAL;

	switch (statusb[0]) {
	default:
		return -EINVAL;
	case '0' + BGQ_BS_INITIALIZED:
		status = BGQ_BS_INITIALIZED;
		break;
	case '0' + BGQ_BS_HALTED:
		status = BGQ_BS_HALTED;
		break;
	}
	rc = bgq_block_state(status);
	if (rc < 0)
		return rc;
	BUG_ON(rc > 0);

	return size;
}

static const struct file_operations bgq_bs_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = bgq_bs_write,
};

static struct miscdevice bgq_bs = {
	.minor = BGQ_BS_MINOR,
	.name = BGQ_BS_NAME,
	.fops = &bgq_bs_fops
};

/* Module initialization functions */
static int __init bgq_bs_module_init(void)
{
	pr_info("Initializing Blue Gene/Q Block Status Device\n");

	/* Register the bgpers misc device ... Ensure an
	   rc of zero, return an error otherwise. */
	if ((misc_register(&bgq_bs)) != 0)
		return -ENOMEM;

	return 0;
}

static void __exit bgq_bs_module_exit(void)
{

	pr_info("Releasing the Blue Gene/Q Block Status Device\n");
	misc_deregister(&bgq_bs);
}

device_initcall(bgq_bs_module_init);
module_exit(bgq_bs_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jay S. Bryant <jsbryant@us.ibm.com>");
MODULE_AUTHOR("Jimi Xenidis <jimix@pobox.com>");
MODULE_DESCRIPTION("IBM Blue Gene/Q Block Status Device Driver");
