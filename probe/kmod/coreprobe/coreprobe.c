/*
 * Prints what the Cortex-A7 cluster says about itself: L2CTLR[25:24] is the
 * core count the cluster was synthesized with, which the efuse cannot change.
 * mt-smp.c read this register before MediaTek switched to the efuse word, so
 * reading it from PL1 is known to work on this SoC. Nothing is written.
 *
 *   insmod coreprobe.ko && dmesg | grep coreprobe && rmmod coreprobe
 *
 * Build: see Makefile (stock IKCONFIG, arm-eabi-4.8, modules_prepare).
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/smp.h>

static void coreprobe_read(void *unused)
{
	u32 midr, mpidr, l2ctlr;

	asm volatile("mrc p15, 0, %0, c0, c0, 0" : "=r"(midr));
	asm volatile("mrc p15, 0, %0, c0, c0, 5" : "=r"(mpidr));
	asm volatile("mrc p15, 1, %0, c9, c0, 2" : "=r"(l2ctlr));
	pr_info("coreprobe: cpu%d MIDR %08x MPIDR %08x L2CTLR %08x -> cluster of %u\n",
		smp_processor_id(), midr, mpidr, l2ctlr, ((l2ctlr >> 24) & 3) + 1);
}

static int __init coreprobe_init(void)
{
	on_each_cpu(coreprobe_read, NULL, 1);
	return 0;
}

static void __exit coreprobe_exit(void)
{
}

module_init(coreprobe_init);
module_exit(coreprobe_exit);
MODULE_LICENSE("GPL");
