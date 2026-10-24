/*
 * swap-accounting-guard - RAM-only runtime guard for the stock swap lock
 * mismatch on the NW-A55 (MT8590) kernel 3.10.26.
 *
 * Defect: swap_info_get() locks p->lock (mm/swapfile.c:576) while
 * swap_info_unlock() unlocks the global swap_lock (mm/swapfile.c:594-597).
 * fs/proc/task_mmu.c:539-546 (smaps) and fs/proc/page.c:107-114
 * (kpageswapn_read) take the first and release the second, so the per-device
 * lock leaks and the next taker spins forever.
 *
 * zram_slot_free_notify also takes a sleeping rwsem below p->lock; its
 * callback is rewritten to free synchronously only after trylock succeeds.
 *
 * Guard: each accounting site's `bl swap_info_get` becomes `mov r0, #0`, so the
 * existing null check skips both mismatched lock operations. smaps loses
 * only the vendor SwapPss figure; /proc/kpageswapn returns -EFAULT.
 *
 * The stock facts live in swap_guard_spec.h and the transaction in
 * swap_guard_txn.h; both are shared with the host tests. Rationale is in
 * DESIGN.md. This file supplies only the device backend.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <linux/swap.h>
#include <linux/mm.h>
#include <linux/stop_machine.h>
#include <asm/rodata.h>
#include <asm/cputype.h>

#include "swap_guard_txn.h"
#include "swap_guard_perm.h"
#include "swap_guard_spec.h"


typedef void (*coherent_kern_fn)(unsigned long, unsigned long);

struct guard {
	coherent_kern_fn icache;
	struct mm_struct *init_mm;
	unsigned long addr[SG_SITE_COUNT];
};

static struct guard g;

/* Real page-table readers for sg_perm_check(). set_memory_rw()/set_memory_ro()
 * returning 0 is not proof of the resulting permission, so the PTE is inspected
 * rather than probed with a same-store that could itself undo the guard. */
static int g_pmd_read(void *ctx, unsigned long addr, unsigned long *val)
{
	pgd_t *pgd = pgd_offset(g.init_mm, addr);
	pud_t *pud;
	pmd_t *pmd;

	(void)ctx;
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		return -1;
	pud = pud_offset(pgd, addr);
	if (pud_none(*pud) || pud_bad(*pud))
		return -1;
	pmd = pmd_offset(pud, addr);
	*val = pmd_val(*pmd);
	return 0;
}

static int g_pte_read(void *ctx, unsigned long addr, unsigned long *val)
{
	pgd_t *pgd = pgd_offset(g.init_mm, addr);
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	int rc = -1;

	(void)ctx;
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		return -1;
	pud = pud_offset(pgd, addr);
	if (pud_none(*pud) || pud_bad(*pud))
		return -1;
	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd) || pmd_bad(*pmd))
		return -1;
	pte = pte_offset_map(pmd, addr);
	if (pte) {
		*val = pte_val(*pte);
		rc = 0;
		pte_unmap(pte);	/* paired on every path */
	}
	return rc;
}

static const struct sg_pt g_pt = { g_pmd_read, g_pte_read };

static int g_perm(unsigned long addr, int *writable)
{
	return sg_perm_check(&g_pt, NULL, addr, writable) == SG_PERM_OK ? 0 : -1;
}

static int g_read(void *ctx, unsigned long addr, unsigned int *out)
{
	(void)ctx;
	return probe_kernel_read(out, (const void *)addr, sizeof(*out)) == 0 ? 0 : -1;
}

/* The store __patch_text() performs, then the instruction maintenance it
 * performs. v7_coherent_kern_range is the V7 implementation of
 * cpu_cache.coherent_kern_range, i.e. what flush_icache_range() calls on this
 * CPU, and it issues no IPI, so it is safe inside the stopped context. */
static int g_write(void *ctx, unsigned long addr, unsigned int word)
{
	struct guard *gd = ctx;

	if (probe_kernel_write((void *)addr, &word, sizeof(word)) != 0)
		return -1;
	gd->icache(addr, addr + sizeof(word));
	return 0;
}

static int g_set_rw(void *ctx, unsigned long addr)
{
	int writable = 0;

	(void)ctx;
	if (set_memory_rw(addr & PAGE_MASK, 1) != 0)
		return -1;
	if (g_perm(addr, &writable) != 0)
		return -1;

	return writable ? 0 : -1;
}

static int g_set_ro(void *ctx, unsigned long addr)
{
	int writable = 1;

	(void)ctx;
	if (set_memory_ro(addr & PAGE_MASK, 1) != 0)
		return -1;
	if (g_perm(addr, &writable) != 0)
		return -1;

	return writable ? -1 : 0;
}

static int g_stopped(void *ctx, int (*fn)(void *), void *data)
{
	(void)ctx;
	/* NULL mask: the callback runs on the first online CPU only and the
	 * others are stopped, so the write is applied exactly once. */
	return stop_machine(fn, data, NULL);
}

static int g_swap(void *ctx, unsigned long *totalswap)
{
	struct sysinfo si;

	(void)ctx;
	/* Takes swap_lock, so it must run outside the stopped context. */
	si_swapinfo(&si);
	*totalswap = si.totalswap;
	return 0;
}

static const struct sg_ops g_ops = {
	g_read, g_write, g_set_rw, g_set_ro, g_stopped, g_swap,
};

static int resolve_symbol(const char *name, unsigned long expect, unsigned long *out)
{
	unsigned long sym = kallsyms_lookup_name(name);

	if (!sym) {
		pr_err("swap-accounting-guard: %s not found\n", name);
		return -ENOENT;
	}
	if (expect && sym != expect) {
		pr_err("swap-accounting-guard: %s at %08lx, expected %08lx\n",
		       name, sym, expect);
		return -ENOENT;
	}
	*out = sym;
	return 0;
}

static int guard_resolve_calls(void)
{
	unsigned long checked;
	unsigned int i;
	int r;

	for (i = 0; i < ARRAY_SIZE(sg_call_targets); i++) {
		r = resolve_symbol(sg_call_targets[i].name,
				   sg_call_targets[i].addr, &checked);
		if (r)
			return r;
	}
	return 0;
}

static void guard_header_bits(void)
{
	/* Compile-time binding of the permission walk's constants to this
	 * kernel's own definitions. If a future tree changes them the build fails
	 * here, rather than letting the walk read the wrong bits. BUILD_BUG_ON is
	 * a statement, so it lives in a function. */
	BUILD_BUG_ON(SG_PTE_PRESENT != L_PTE_PRESENT);
	BUILD_BUG_ON(SG_PTE_RDONLY != L_PTE_RDONLY);
	BUILD_BUG_ON(SG_PMD_TYPE_TABLE != PMD_TYPE_TABLE);
	BUILD_BUG_ON(SG_PMD_TYPE_MASK != PMD_TYPE_MASK);
}

static int guard_resolve(void)
{
	unsigned long swap_info_get;
	unsigned long icache;
	int i, r;

	guard_header_bits();
	r = guard_resolve_calls();
	if (r)
		return r;
	r = resolve_symbol("swap_info_get", SG_STOCK_SWAP_INFO_GET,
			   &swap_info_get);
	if (r)
		return r;
	for (i = 0; i < SG_SITE_COUNT; i++) {
		r = resolve_symbol(sg_spec[i].symbol, sg_spec[i].expect_addr,
				   &g.addr[i]);
		if (r)
			return r;
		g.addr[i] += sg_spec[i].offset;
	}
	r = resolve_symbol(SG_ICACHE_SYMBOL, SG_ICACHE_STOCK_ADDR, &icache);
	if (r)
		return r;
	g.icache = (coherent_kern_fn)icache;
	r = resolve_symbol("init_mm", SG_STOCK_INIT_MM,
			   (unsigned long *)&g.init_mm);
	if (r)
		return r;
	if ((read_cpuid_id() & SG_CPUID_MASK) != SG_CPUID_REQUIRED) {
		pr_err("swap-accounting-guard: cpu id %08x is not the expected A7\n",
		       read_cpuid_id());
		return -ENODEV;
	}

	return 0;
}

/* Report the per-site evidence. Logged after the stopped context returns. */
static void guard_report(const struct sg_txn *t)
{
	int i;

	for (i = 0; i < SG_SITE_COUNT; i++) {
		const struct sg_site_result *r = &t->site[i];

		pr_info("swap-accounting-guard: site %d %s+%#lx addr %08lx old %08x old_read_ok %d new %08x write_rc %d patch_readback %08x patch_readback_ok %d patched %d rw_attempted %d ro_attempted %d ro_verified %d rollback_write_rc %d rollback_readback %08x rollback_read_ok %d rolled_back %d validate_only %d stopped_word %08x stopped_read_ok %d\n",
			i, sg_spec[i].symbol, sg_spec[i].offset, r->addr,
			r->old_word, r->old_read_ok, r->new_word, r->write_rc,
			r->readback, r->readback_ok, r->patched,
			r->rw_attempted, r->ro_attempted, r->ro_verified,
			r->rollback_write_rc, r->rollback_readback,
			r->rollback_read_ok, r->rolled_back, r->validate_only,
			r->stopped_word, r->stopped_read_ok);
	}
	pr_info("swap-accounting-guard: writes %d/%d rollback %d/%d failed %d rw_ops %d ro_ops_restored %d ro_ops_failed %d stopped %d ran %d guards %d/%d required %d\n",
		t->writes_succeeded, t->writes_attempted, t->rollback_succeeded,
		t->rollback_attempted, t->rollback_failed, t->pages_rw_attempted,
		t->pages_ro_restored, t->pages_ro_failed, t->stopped_calls,
		t->stage2_ran, t->guards_verified, SG_SITE_COUNT,
		t->writes_required);
}

static int __init guard_init(void)
{
	/* Keep multiword load evidence off the ARM kernel stack. */
	static struct sg_txn txn;
	int r;

	pr_info("swap-accounting-guard: loading (RAM-only, no storage writes)\n");

	r = guard_resolve();
	if (r)
		return r;

	r = sg_run(&g_ops, &g, sg_spec, g.addr, SG_SITE_COUNT, &txn);
	guard_report(&txn);
	if (r != SG_TXN_OK)
		pr_err("swap-accounting-guard: refused (%d)\n", r);

	return r == SG_TXN_OK ? 0 : -EINVAL;
}

module_init(guard_init);
/* No module_exit on purpose: the guard cannot be unloaded, so the vulnerable
 * accounting cannot be restored while swap is live. Reboot is the recovery. */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("nw-a55 bring-up");
MODULE_DESCRIPTION("RAM-only swap accounting and non-blocking zram notify guards");
