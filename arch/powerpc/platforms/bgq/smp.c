#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/elf.h>

#include <asm/cputhreads.h>

#include "bgq.h"

static int __init smp_bgq_probe(void)
{
	return num_possible_cpus();
}

static int smp_bgq_cpu_bootable(unsigned int nr)
{
	/*
	 * Special case - we inhibit secondary thread startup
	 * during boot if the user requests it.
	 */
	if (system_state < SYSTEM_RUNNING && cpu_has_feature(CPU_FTR_SMT)) {
		if (!smt_enabled_at_boot && cpu_thread_in_core(nr) != 0)
			return 0;
		if (smt_enabled_at_boot &&
		    cpu_thread_in_core(nr) >= smt_enabled_at_boot)
			return 0;
	}
	return 1;
}

static int __devinit smp_bgq_kick_cpu(int nr)
{
	struct device_node *np;
	int tid;
	const char *enable_method;

	if (nr < 0 || nr >= num_possible_cpus())
		return -ENOENT;

	np = of_get_cpu_node(nr, &tid);
	if (!np)
		return -ENODEV;

	enable_method = of_get_property(np, "enable-method", NULL);
	if (!enable_method) {
		pr_err("CPU%d has no enable-method\n", nr);
		return -ENOENT;
	}
	pr_devel("CPU%d has enable-method: \"%s\"\n", nr, enable_method);

	if (strcmp(enable_method, "kexec") != 0) {
		pr_err("CPU%d: This kernel does not support the \"%s\"\n",
		       nr, enable_method);
		return -EINVAL;
	}

	/*
	 * The processor is currently spinning, waiting for the
	 * cpu_start field to become non-zero.	After we set
	 * cpu_start, the processor will continue on to
	 * secondary_start
	 */
	paca[nr].cpu_start = 1;

	/* barrier so other CPU can see it */
	smp_mb();

	return 0;
}

static void __devinit smp_bgq_setup_cpu(int cpu)
{
	bgq_setup_cpu(cpu);
}

static struct smp_ops_t bgq_smp_ops = {
	.probe		= smp_bgq_probe,
	.kick_cpu	= smp_bgq_kick_cpu,
	.cpu_bootable	= smp_bgq_cpu_bootable,
	.setup_cpu	= smp_bgq_setup_cpu,
	.cause_ipi	= bgq_cause_ipi,
	.message_pass	= NULL, /* use the muxed_ipi stuff */
};

void __init bgq_setup_smp(void)
{
	smp_ops = &bgq_smp_ops;
}
