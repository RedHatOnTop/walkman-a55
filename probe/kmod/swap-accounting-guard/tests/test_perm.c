/*
 * Host tests for the permission walker (swap_guard_perm.h) and for the spec
 * constants the module loads with. The walker is real module code; only the
 * pmd/pte readers are mocked here, so absent/bad PMD and absent PTE paths are
 * exercised without a device.
 */
#include <stdio.h>
#include "../swap_guard_perm.h"
#include "../swap_guard_spec.h"

static int failures;

struct pt { unsigned long pmd, pte; int pmd_rc, pte_rc; };

static int r_pmd(void *ctx, unsigned long addr, unsigned long *val)
{
	struct pt *p = ctx; (void)addr;
	if (p->pmd_rc) return p->pmd_rc;
	*val = p->pmd; return 0;
}

static int r_pte(void *ctx, unsigned long addr, unsigned long *val)
{
	struct pt *p = ctx; (void)addr;
	if (p->pte_rc) return p->pte_rc;
	*val = p->pte; return 0;
}

static const struct sg_pt ops = { r_pmd, r_pte };

static void check(const char *name, int got, int want)
{
	if (got == want) printf("PASS %-46s (%d)\n", name, got);
	else { printf("FAIL %-46s got=%d want=%d\n", name, got, want); failures++; }
}

int main(void)
{
	struct pt p;
	int w = -1;

	p.pmd = SG_PMD_TYPE_TABLE; p.pte = SG_PTE_PRESENT; p.pmd_rc = p.pte_rc = 0;
	check("coarse table + present -> OK",
	      sg_perm_check(&ops, &p, 0xc01acb90ul, &w), SG_PERM_OK);
	check("present, no RDONLY -> writable", w, 1);

	p.pte = SG_PTE_PRESENT | SG_PTE_RDONLY;
	check("present + RDONLY -> OK", sg_perm_check(&ops, &p, 0, &w), SG_PERM_OK);
	check("RDONLY -> not writable", w, 0);

	p.pmd = 0; p.pte = SG_PTE_PRESENT;
	check("absent PMD -> NO_PMD", sg_perm_check(&ops, &p, 0, &w), SG_PERM_NO_PMD);

	p.pmd = 2;			/* section, not a coarse table */
	check("non-table PMD -> BAD_PMD", sg_perm_check(&ops, &p, 0, &w), SG_PERM_BAD_PMD);

	p.pmd = SG_PMD_TYPE_TABLE; p.pte = 0;
	check("absent PTE -> NO_PTE", sg_perm_check(&ops, &p, 0, &w), SG_PERM_NO_PTE);

	p.pte = SG_PTE_PRESENT; p.pmd_rc = -1;
	check("pmd reader error -> NO_PMD", sg_perm_check(&ops, &p, 0, &w), SG_PERM_NO_PMD);
	p.pmd_rc = 0; p.pte_rc = -1;
	check("pte reader error -> NO_PTE", sg_perm_check(&ops, &p, 0, &w), SG_PERM_NO_PTE);

	check("null pt -> BAD_ARG", sg_perm_check(NULL, &p, 0, &w), SG_PERM_BAD_ARG);
	check("null writable -> BAD_ARG", sg_perm_check(&ops, &p, 0, NULL), SG_PERM_BAD_ARG);

	/* the spec's masked CPUID comparison must be satisfiable by the measured
	 * MIDR: this is what a mismatched mask/required pair silently broke. */
	check("measured MIDR masks to SG_CPUID_REQUIRED",
	      ((0x410fc073u & SG_CPUID_MASK) == SG_CPUID_REQUIRED), 1);
	check("a different part does not match",
	      ((0x410fc0f3u & SG_CPUID_MASK) == SG_CPUID_REQUIRED), 0);
	check("spec sites == SG_SITE_COUNT", SG_SITE_COUNT, 22);
	/* the shared spec must carry the exact init_mm the module resolves */
	check("spec SG_STOCK_INIT_MM", (int)SG_STOCK_INIT_MM, (int)0xc0b5d840ul);
	check("spec SG_STOCK_SWAP_INFO_GET", (int)SG_STOCK_SWAP_INFO_GET, (int)0xc0150448ul);
	check("perm constants match the ARM 2-level bits",
	      (SG_PTE_PRESENT == (1ul << 0)) && (SG_PTE_RDONLY == (1ul << 7)) &&
	      (SG_PMD_TYPE_TABLE == 1ul) && (SG_PMD_TYPE_MASK == 3ul), 1);

	if (failures) { printf("\nFAILURES: %d\n", failures); return 1; }
	printf("\nALL PASS\n");
	return 0;
}
