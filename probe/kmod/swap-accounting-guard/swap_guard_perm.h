/*
 * swap_guard_perm.h - page-table permission check, in host-testable form.
 *
 * The module supplies real pmd/pte readers; the host tests supply mocks, so the
 * walk itself (absent/bad PMD, absent PTE, write bit) is exercised without a
 * device. set_memory_rw()/set_memory_ro() returning 0 is not proof of the
 * resulting permission, which is why the PTE is inspected instead of probing
 * with a same-value store that could itself undo the guard.
 */
#ifndef SWAP_GUARD_PERM_H
#define SWAP_GUARD_PERM_H

/* ARM 2-level: PMD type in bits [1:0], 1 = coarse page table. */
#define SG_PMD_TYPE_MASK	3ul
#define SG_PMD_TYPE_TABLE	1ul
#define SG_PTE_PRESENT		(1ul << 0)
#define SG_PTE_RDONLY		(1ul << 7)

enum sg_perm_result {
	SG_PERM_OK = 0,
	SG_PERM_NO_PMD = -1,
	SG_PERM_BAD_PMD = -2,
	SG_PERM_NO_PTE = -3,
	SG_PERM_BAD_ARG = -4,
};

struct sg_pt {
	/* 0 on success, *val = raw entry */
	int (*pmd)(void *ctx, unsigned long addr, unsigned long *val);
	int (*pte)(void *ctx, unsigned long addr, unsigned long *val);
};

static inline int sg_perm_check(const struct sg_pt *pt, void *ctx,
				unsigned long addr, int *writable)
{
	unsigned long pmd = 0, pte = 0;

	if (!pt || !pt->pmd || !pt->pte || !writable)
		return SG_PERM_BAD_ARG;
	if (pt->pmd(ctx, addr, &pmd) != 0 || pmd == 0)
		return SG_PERM_NO_PMD;
	if ((pmd & SG_PMD_TYPE_MASK) != SG_PMD_TYPE_TABLE)
		return SG_PERM_BAD_PMD;
	if (pt->pte(ctx, addr, &pte) != 0 || !(pte & SG_PTE_PRESENT))
		return SG_PERM_NO_PTE;

	*writable = (pte & SG_PTE_RDONLY) ? 0 : 1;
	return SG_PERM_OK;
}

#endif /* SWAP_GUARD_PERM_H */
