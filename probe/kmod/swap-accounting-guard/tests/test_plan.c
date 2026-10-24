/*
 * Host tests for the pure planner (swap_guard_plan.h), the module's real
 * decision logic.
 *
 * Stock facts: smaps_pte_range c01acabc + 0xd4 = c01acb90 word 0xebfe8e2c;
 * kpageswapn_read c01b996c + 0x38 = c01b99a4 word 0xebfe5aa7; replacement
 * mov r0,#0 = 0xe3a00000. Accounting sites require exact stock words; only
 * the validation-only callback mcount site also accepts the ftrace NOP.
 */
#include <stdio.h>
#include <string.h>
#include "../swap_guard_plan.h"
#include "../swap_guard_spec.h"

static int failures;

/* the module's own spec, not a separate fixture */
#define sites sg_spec

static void check(const char *name, int got, int want)
{
	if (got == want) {
		printf("PASS %-46s (%d)\n", name, got);
	} else {
		printf("FAIL %-46s got=%d want=%d\n", name, got, want);
		failures++;
	}
}

static void st(struct sg_site_state *s, unsigned int a, unsigned int b)
{
	int i;

	for (i = 0; i < SG_SITE_COUNT; i++) {
		s[i].word = sites[i].stock_word;
		s[i].read_ok = 1;
	}
	s[0].word = a; s[0].read_ok = 1;
	s[1].word = b; s[1].read_ok = 1;
}

int main(void)
{
	struct sg_site_state s[SG_SITE_COUNT];
	int need, r;
	struct sg_site bad[SG_SITE_COUNT];

	st(s, 0xebfe8e2cu, 0xebfe5aa7u);
	r = sg_plan(sites, s, SG_SITE_COUNT, &need);
	check("stock/stock -> PATCH", r, SG_PLAN_PATCH);
	check("guarded word count", SG_SITE_COUNT, 22);
	check("changed word count", need, 11);

	st(s, 0xdeadbeefu, 0xebfe5aa7u);
	check("first wrong word -> BAD_WORD",
	      sg_plan(sites, s, SG_SITE_COUNT, &need), SG_PLAN_BAD_WORD);

	st(s, 0xebfe8e2cu, 0xdeadbeefu);
	check("second wrong word -> BAD_WORD",
	      sg_plan(sites, s, SG_SITE_COUNT, &need), SG_PLAN_BAD_WORD);

	st(s, 0xe3a00000u, 0xe3a00000u);
	check("already patched -> BAD_WORD",
	      sg_plan(sites, s, SG_SITE_COUNT, &need), SG_PLAN_BAD_WORD);

	st(s, 0xe3a00000u, 0xebfe5aa7u);
	check("half patched -> BAD_WORD",
	      sg_plan(sites, s, SG_SITE_COUNT, &need), SG_PLAN_BAD_WORD);

	st(s, 0xebfe8e2cu, 0xebfe5aa7u); s[0].read_ok = 0;
	check("first read failure -> BAD_READ",
	      sg_plan(sites, s, SG_SITE_COUNT, &need), SG_PLAN_BAD_READ);

	st(s, 0xebfe8e2cu, 0xebfe5aa7u); s[1].read_ok = 0;
	check("second read failure -> BAD_READ",
	      sg_plan(sites, s, SG_SITE_COUNT, &need), SG_PLAN_BAD_READ);

	st(s, 0xebfe8e2cu, 0xebfe5aa7u);
	check("null state -> BAD_ARG",
	      sg_plan(sites, NULL, SG_SITE_COUNT, &need), SG_PLAN_BAD_ARG);
	memcpy(bad, sites, sizeof(bad)); bad[0].alternate_word = SG_FTRACE_NOP;
	check("write-site alternate -> BAD_ARG",
	      sg_plan(bad, s, SG_SITE_COUNT, &need), SG_PLAN_BAD_ARG);
	check("write-site alternate does not match", sg_site_matches(&bad[0], SG_FTRACE_NOP), 0);
	check("null sites -> BAD_ARG",
	      sg_plan(NULL, s, SG_SITE_COUNT, &need), SG_PLAN_BAD_ARG);
	check("short count -> BAD_ARG",
	      sg_plan(sites, s, SG_SITE_COUNT - 1, &need), SG_PLAN_BAD_ARG);
	check("null need -> BAD_ARG",
	      sg_plan(sites, s, SG_SITE_COUNT, NULL), SG_PLAN_BAD_ARG);

	check("site0 expect addr", (int)sites[0].expect_addr, (int)0xc01acabcul);
	check("site1 expect addr", (int)sites[1].expect_addr, (int)0xc01b996cul);
	check("site0 offset", (int)sites[0].offset, 0xd4);
	check("site1 offset", (int)sites[1].offset, 0x38);
	check("site0 stock word", (int)sites[0].stock_word, (int)0xebfe8e2cu);
	check("site1 stock word", (int)sites[1].stock_word, (int)0xebfe5aa7u);
	check("patch word is mov r0,#0", (int)sites[0].patch_word, (int)0xe3a00000u);

	check("kpageswapn patch word", (int)sites[1].patch_word, (int)0xe3a00000u);

	if (failures) { printf("\nFAILURES: %d\n", failures); return 1; }
	printf("\nALL PASS\n");
	return 0;
}
