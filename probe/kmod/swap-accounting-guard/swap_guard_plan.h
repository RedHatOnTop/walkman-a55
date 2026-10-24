/*
 * swap_guard_plan.h - pure decision logic for the swap-accounting guard.
 *
 * Shared verbatim between the kernel module and the host tests, so the tests
 * exercise the module's real guard rather than a reimplementation. Nothing here
 * does I/O: the caller supplies the words it read and the planner decides
 * whether any write may happen.
 *
 * The guard exists because the stock kernel takes and releases mismatched locks
 * on the swap accounting path: swap_info_get() locks p->lock
 * (mm/swapfile.c:576) while swap_info_unlock() unlocks the global swap_lock
 * (mm/swapfile.c:594-597). The smaps walk (fs/proc/task_mmu.c:539-546) and
 * kpageswapn_read (fs/proc/page.c:107-114) take the first and release the
 * second, leaking the per-device lock.
 *
 * Fail-closed contract: only SG_PLAN_PATCH allows writes. An already-patched
 * site is rejected, not accepted: the guard is defined against the ORIGINAL
 * stock words. The validation-only mcount site also accepts the exact ARM
 * dynamic-ftrace NOP; it is never written or rolled back.
 */
#ifndef SWAP_GUARD_PLAN_H
#define SWAP_GUARD_PLAN_H

#define SG_SITE_COUNT 22
#define SG_WRITE_COUNT 11

struct sg_site {
	const char *symbol;	/* kallsyms symbol the site lives in */
	unsigned long expect_addr;	/* exact stock address of that symbol */
	unsigned long offset;	/* byte offset from the symbol to the site */
	unsigned int stock_word;	/* exact stock instruction, verified */
	unsigned int patch_word;	/* replacement instruction */
	unsigned int alternate_word;	/* optional live word, validation-only */
};

struct sg_site_state {
	unsigned int word;	/* the word read at the site */
	int read_ok;		/* 0 when the read failed */
};

enum sg_plan_result {
	SG_PLAN_PATCH = 0,	/* every site matches the spec: writes allowed */
	SG_PLAN_BAD_READ = -1,	/* a read failed: write nothing */
	SG_PLAN_BAD_WORD = -2,	/* a word is not an allowed word: write nothing */
	SG_PLAN_BAD_ARG = -3,	/* bad arguments: write nothing */
};

static inline int sg_site_writes(const struct sg_site *site)
{
	return site->stock_word != site->patch_word;
}

static inline int sg_site_matches(const struct sg_site *site, unsigned int word)
{
	return word == site->stock_word ||
	       (!sg_site_writes(site) && site->alternate_word &&
		word == site->alternate_word);
}

static inline int sg_plan(const struct sg_site *sites,
			  const struct sg_site_state *st, int n, int *need_out)
{
	int i, need = 0;

	if (!sites || !st || !need_out || n != SG_SITE_COUNT)
		return SG_PLAN_BAD_ARG;

	for (i = 0; i < n; i++) {
		if (sites[i].alternate_word && sg_site_writes(&sites[i]))
			return SG_PLAN_BAD_ARG;
		if (!st[i].read_ok)
			return SG_PLAN_BAD_READ;
		if (!sg_site_matches(&sites[i], st[i].word))
			return SG_PLAN_BAD_WORD;
		need += sg_site_writes(&sites[i]);
	}

	*need_out = need;
	return SG_PLAN_PATCH;
}

#endif /* SWAP_GUARD_PLAN_H */
