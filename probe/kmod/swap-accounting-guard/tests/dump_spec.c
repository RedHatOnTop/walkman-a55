#include <stdio.h>
#include "../swap_guard_spec.h"

int main(void)
{
	unsigned int i;

	printf("SPEC %u %u\n", SG_SITE_COUNT, SG_WRITE_COUNT);
	for (i = 0; i < SG_SITE_COUNT; i++)
		printf("SITE %s %08lx %lx %08x %08x %08x\n", sg_spec[i].symbol,
		       sg_spec[i].expect_addr, sg_spec[i].offset,
		       sg_spec[i].stock_word, sg_spec[i].patch_word,
		       sg_spec[i].alternate_word);
	for (i = 0; i < sizeof(sg_call_targets) / sizeof(sg_call_targets[0]); i++)
		printf("TARGET %s %08lx\n", sg_call_targets[i].name,
		       sg_call_targets[i].addr);
	return ferror(stdout) ? 1 : 0;
}
