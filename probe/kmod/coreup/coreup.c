/*
 * Powers one fused-off Cortex-A7 core (cpu2 or cpu3) on, lets it run a stub
 * that reports through a mailbox and parks in WFI, then powers it off again.
 * The core never joins Linux: this kernel is NR_CPUS=2. Nothing persists.
 *
 *   insmod coreup_mod.ko cpu=2 && dmesg | grep coreup && rmmod coreup_mod
 *   insmod coreup_mod.ko dry=1     print the registers it would touch, no writes
 *
 * The register sequence is spm_mtcmos_ctrl_cpu2/3 from mt_spm_mtcmos.c, with
 * a timeout on every ack the kernel version waits on forever: a fuse that
 * gates the power domain in hardware would otherwise hang the kernel with
 * interrupts off. The wake path is mt_wakeup_cpu from mt-smp.c with
 * CONFIG_MTK_IN_HOUSE_TEE_SUPPORT: the slave magic and boot address go both to
 * SRAMROM/INFRACFG and to the TEE's copies in the boot share page. The jump
 * registers that mt_platform_smp_prepare_cpus sets once are pointed at the stub
 * too, so the core lands there whichever one the boot path reads. cpu1 hotplug
 * shares all of them, so hotplug is held off and every one is restored.
 *
 * After power-on a wake kick is sent (SEV for a WFE spin, plus a GIC SGI to the
 * physical core for a WFI spin), matching smp_cross_call on the stock path;
 * nokick=1 skips it. Before power-on the TEE reset vector is also pointed at
 * the stub via MC_FC_SET_RESET_VECTOR (nosmc=1 skips); the stock IN_HOUSE_TEE
 * path never sets it, so this probes whether the monitor honors it at runtime.
 */
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#define GIC_DIST_BASE		0xF0211000
#define GIC_DIST_SOFTINT	(GIC_DIST_BASE + 0xF00)
#define MC_FC_SET_RESET_VECTOR	(-301)

#define SPM_BASE		0xF0006000
#define SPM_POWERON_CONFIG_SET	(SPM_BASE + 0x0000)
#define SPM_PWR_STATUS		(SPM_BASE + 0x060c)
#define SPM_PWR_STATUS_S	(SPM_BASE + 0x0610)
#define SPM_SLEEP_TIMER_STA	(SPM_BASE + 0x0720)
#define SPM_PROJECT_CODE	0xb16
#define SRAMROM_BASE		0xF0202000
#define INFRACFG_AO_BASE	0xF0001000
#define BOOT_ADDR		(INFRACFG_AO_BASE + 0x800)
#define SLAVE_JUMP_REG		(SRAMROM_BASE + 0x34)
#define BOOT_SHARE_BASE		0xC0002000
#define NS_SLAVE_BOOT_ADDR	(BOOT_SHARE_BASE + 1012)
#define NS_SLAVE_MAGIC_REG	(BOOT_SHARE_BASE + 1016)
#define NS_SLAVE_JUMP_REG	(BOOT_SHARE_BASE + 1020)
#define DBG_MON_SEL		0xF020021c
#define DBG_MON			0xF0200014

#define SRAM_ISOINT_B	(1U << 6)
#define SRAM_CKISO	(1U << 5)
#define PWR_CLK_DIS	(1U << 4)
#define PWR_ON_S	(1U << 3)
#define PWR_ON		(1U << 2)
#define PWR_ISO		(1U << 1)
#define PWR_RST_B	(1U << 0)
#define L1_PDN_ACK	(1U << 8)
#define L1_PDN		(1U << 0)

struct core {
	unsigned long pwr_con, l1_pdn, magic_reg;
	u32 fc, sleep, magic, dbg_sel;
};

static const struct core cores[] = {
	[2] = { SPM_BASE + 0x021c, SPM_BASE + 0x026c, SRAMROM_BASE + 0x3C,
		1U << 10, 1U << 17, 0x4C415332, 0x1 },
	[3] = { SPM_BASE + 0x0220, SPM_BASE + 0x0274, SRAMROM_BASE + 0x40,
		1U << 9, 1U << 18, 0x41534C33, 0x3 },
};

static int cpu = 2;
module_param(cpu, int, 0444);
static int dry;
module_param(dry, int, 0444);
static int nokick;
module_param(nokick, int, 0444);
static int nosmc;
module_param(nosmc, int, 0444);

extern char coreup_stub_start[], coreup_stub_end[];
extern char coreup_stub_want[], coreup_stub_fallback[], coreup_stub_mailbox[];

static unsigned long boot_regs[] = {
	BOOT_ADDR, SLAVE_JUMP_REG, NS_SLAVE_BOOT_ADDR, NS_SLAVE_JUMP_REG,
};

static u32 rd(unsigned long a)
{
	return readl((void __iomem *)a);
}

static void wr(unsigned long a, u32 v)
{
	writel(v, (void __iomem *)a);
	dsb();
}

static void set(unsigned long a, u32 bits)
{
	wr(a, rd(a) | bits);
}

static void clr(unsigned long a, u32 bits)
{
	wr(a, rd(a) & ~bits);
}

/* Polls up to 10 ms; returns 0 when (reg & mask) == want. */
static int wait_for(unsigned long a, u32 mask, u32 want)
{
	int i;

	for (i = 0; i < 10000; i++) {
		if ((rd(a) & mask) == want)
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int wait_status(u32 fc, u32 want)
{
	int i;

	for (i = 0; i < 10000; i++) {
		if ((rd(SPM_PWR_STATUS) & fc) == want &&
		    (rd(SPM_PWR_STATUS_S) & fc) == want)
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int sec_call(u32 cmd, u32 p0, u32 p1, u32 p2)
{
	register u32 r0 __asm__("r0") = cmd;
	register u32 r1 __asm__("r1") = p0;
	register u32 r2 __asm__("r2") = p1;
	register u32 r3 __asm__("r3") = p2;

	__asm__ volatile(".arch_extension sec\n\tsmc 0\n"
			 : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3));
	return r0;
}

static void wake_core(int phys)
{
	if (nokick)
		return;
	/* A WFE spin in the bootrom wakes on an event, a WFI spin on an
	 * interrupt: send both - SEV, then an SGI to the physical core's GIC
	 * target, as smp_cross_call kicks a secondary on the stock path. */
	dsb();
	asm volatile("sev" : : : "memory");
	wr(GIC_DIST_SOFTINT, 1U << (16 + phys));
}

static void power_on(const struct core *c, const char **failed)
{
	*failed = NULL;
	set(c->pwr_con, PWR_ON);
	udelay(1);
	set(c->pwr_con, PWR_ON_S);
	if (wait_status(c->fc, c->fc)) {
		*failed = "PWR_STATUS ack";
		return;
	}
	clr(c->pwr_con, PWR_CLK_DIS);
	clr(c->l1_pdn, L1_PDN);
	if (wait_for(c->l1_pdn, L1_PDN_ACK, 0)) {
		*failed = "L1_PDN ack";
		return;
	}
	set(c->pwr_con, SRAM_ISOINT_B);
	clr(c->pwr_con, SRAM_CKISO);
	clr(c->pwr_con, PWR_ISO);
	set(c->pwr_con, PWR_RST_B);
}

static void power_off(const struct core *c, int *in_wfi, int *off_ok)
{
	*in_wfi = !wait_for(SPM_SLEEP_TIMER_STA, c->sleep, c->sleep);
	set(c->pwr_con, SRAM_CKISO);
	clr(c->pwr_con, SRAM_ISOINT_B);
	set(c->l1_pdn, L1_PDN);
	wait_for(c->l1_pdn, L1_PDN_ACK, L1_PDN_ACK);
	set(c->pwr_con, PWR_ISO);
	wr(c->pwr_con, (rd(c->pwr_con) | PWR_CLK_DIS) & ~PWR_RST_B);
	clr(c->pwr_con, PWR_ON);
	clr(c->pwr_con, PWR_ON_S);
	*off_ok = !wait_status(c->fc, 0);
}

static int __init coreup_init(void)
{
	void (*lock)(unsigned long *), (*unlock)(unsigned long *);
	unsigned long entry, flags;
	size_t len = coreup_stub_end - coreup_stub_start;
	const struct core *c;
	const char *failed;
	u32 saved_boot[ARRAY_SIZE(boot_regs)], saved_magic, saved_ns_magic;
	u32 *mb, mon = 0, mon_sel;
	int i, in_wfi, off_ok, ran = 0, smc_ret = 0;
	dma_addr_t phys;
	void *page;

	if (cpu != 2 && cpu != 3)
		return -EINVAL;
	c = &cores[cpu];
	lock = (void *)kallsyms_lookup_name("spm_mtcmos_cpu_lock");
	unlock = (void *)kallsyms_lookup_name("spm_mtcmos_cpu_unlock");
	entry = kallsyms_lookup_name("mt_secondary_startup");
	if (!lock || !unlock || !entry)
		return -ENOENT;
	if (dry) {
		pr_info("coreup: dry PWR_STATUS %08x/%08x SLEEP_TIMER_STA %08x mt_secondary_startup phys %08lx\n",
			rd(SPM_PWR_STATUS), rd(SPM_PWR_STATUS_S), rd(SPM_SLEEP_TIMER_STA),
			(unsigned long)virt_to_phys((void *)entry));
		pr_info("coreup: dry boot regs %08x %08x %08x %08x magic %08x %08x %08x ns %08x\n",
			rd(boot_regs[0]), rd(boot_regs[1]), rd(boot_regs[2]), rd(boot_regs[3]),
			rd(SRAMROM_BASE + 0x38), rd(cores[2].magic_reg), rd(cores[3].magic_reg),
			rd(NS_SLAVE_MAGIC_REG));
		pr_info("coreup: dry PWR_CON fc1 %08x fc2 %08x fc3 %08x L1_PDN fc1 %08x fc2 %08x fc3 %08x\n",
			rd(SPM_BASE + 0x0218), rd(cores[2].pwr_con), rd(cores[3].pwr_con),
			rd(SPM_BASE + 0x0264), rd(cores[2].l1_pdn), rd(cores[3].l1_pdn));
		return 0;
	}
	if (rd(SPM_PWR_STATUS) & c->fc) {
		pr_info("coreup: cpu%d is already powered, not touching it\n", cpu);
		return -EBUSY;
	}

	page = dma_alloc_coherent(NULL, PAGE_SIZE, &phys, GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	memcpy(page, coreup_stub_start, len);
	*(u32 *)(page + (coreup_stub_want - coreup_stub_start)) = cpu;
	*(u32 *)(page + (coreup_stub_fallback - coreup_stub_start)) = virt_to_phys((void *)entry);
	mb = page + (coreup_stub_mailbox - coreup_stub_start);
	dsb();

	get_online_cpus();
	for (i = 0; i < ARRAY_SIZE(boot_regs); i++)
		saved_boot[i] = rd(boot_regs[i]);
	saved_magic = rd(c->magic_reg);
	saved_ns_magic = rd(NS_SLAVE_MAGIC_REG);
	wr(c->magic_reg, c->magic);
	wr(NS_SLAVE_MAGIC_REG, c->magic);
	for (i = 0; i < ARRAY_SIZE(boot_regs); i++)
		wr(boot_regs[i], phys);
	wr(SPM_POWERON_CONFIG_SET, (SPM_PROJECT_CODE << 16) | (1U << 0));
	if (!nosmc)
		smc_ret = sec_call(MC_FC_SET_RESET_VECTOR, phys, cpu, 0);

	lock(&flags);
	power_on(c, &failed);
	unlock(&flags);

	if (!failed) {
		wake_core(cpu);
		for (i = 0; i < 200; i++) {
			if (ACCESS_ONCE(mb[3]) == 0xc0dec0de) {
				ran = 1;
				break;
			}
			if (i < 10)
				wake_core(cpu);
			msleep(1);
		}
		if (!ran) {
			mon_sel = rd(DBG_MON_SEL);
			wr(DBG_MON_SEL, c->dbg_sel);
			mon = rd(DBG_MON);
			wr(DBG_MON_SEL, mon_sel);
		}
	}

	lock(&flags);
	power_off(c, &in_wfi, &off_ok);
	unlock(&flags);

	for (i = 0; i < ARRAY_SIZE(boot_regs); i++)
		wr(boot_regs[i], saved_boot[i]);
	wr(c->magic_reg, saved_magic);
	wr(NS_SLAVE_MAGIC_REG, saved_ns_magic);
	put_online_cpus();

	if (failed)
		pr_info("coreup: cpu%d no %s, PWR_STATUS %08x/%08x\n", cpu, failed,
			rd(SPM_PWR_STATUS), rd(SPM_PWR_STATUS_S));
	else if (ran)
		pr_info("coreup: cpu%d RAN: MPIDR %08x MIDR %08x count %x\n", cpu,
			mb[0], mb[1], mb[2]);
	else
		pr_info("coreup: cpu%d powered but no report; mailbox %08x %08x %08x, dbg monitor %08x\n",
			cpu, mb[0], mb[1], mb[2], mon);
	pr_info("coreup: cpu%d wfi %d, off %d, boot regs restored to %08x %08x %08x %08x\n",
		cpu, in_wfi, off_ok, rd(boot_regs[0]), rd(boot_regs[1]),
		rd(boot_regs[2]), rd(boot_regs[3]));
	pr_info("coreup: cpu%d set_reset_vector smc_ret %d\n", cpu, smc_ret);

	dma_free_coherent(NULL, PAGE_SIZE, page, phys);
	return 0;
}

static void __exit coreup_exit(void)
{
}

module_init(coreup_init);
module_exit(coreup_exit);
MODULE_LICENSE("GPL");
