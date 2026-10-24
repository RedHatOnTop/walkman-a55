/*
 * swap_guard_spec.h - the immutable stock facts, in one place.
 *
 * Included by both the module and the host tests, so the tests verify the
 * module's actual values rather than a separate fixture.
 *
 * Verified against the stock kernel: vmlinux.bin disassembly (base
 * _text = 0xc0008000) and the running /proc/kallsyms.
 */
#ifndef SWAP_GUARD_SPEC_H
#define SWAP_GUARD_SPEC_H

#include "swap_guard_plan.h"

#define SG_STOCK_SWAP_INFO_GET	0xc0150448ul	/* both sites call this */
#define SG_STOCK_INIT_MM	0xc0b5d840ul	/* init_mm, the kernel's own mm */
#define SG_STOCK_SMAPS		0xc01acabcul
#define SG_STOCK_KPAGESWAPN	0xc01b996cul
#define SG_SMAPS_OFFSET		0xd4u		/* c01acb90: bl swap_info_get */
#define SG_KPAGESWAPN_OFFSET	0x38u		/* c01b99a4: bl swap_info_get */
#define SG_SMAPS_WORD		0xebfe8e2cu
#define SG_KPAGESWAPN_WORD	0xebfe5aa7u
#define SG_STOCK_ZRAM_NOTIFY	0xc03481d8ul
#define SG_STOCK_TRYLOCK	0xc007bc4cul
#define SG_STOCK_ZRAM_FREE	0xc03480e8ul
#define SG_STOCK_UP_WRITE	0xc007bc84ul
#define SG_STOCK_TRYLOCK_INNER	0xc02c55ecul
#define SG_STOCK_UP_WRITE_INNER	0xc02c568cul

#define SG_FTRACE_NOP		0xe8bd4000u	/* ARM ftrace.c:28, pop {lr} */

#define SG_PATCH_WORD		0xe3a00000u	/* mov r0, #0 */

#define SG_ICACHE_SYMBOL	"v7_coherent_kern_range"
#define SG_ICACHE_STOCK_ADDR	0xc001ec80ul
#define SG_ICACHE_REQUIRED	1

#define SG_CPUID_REQUIRED	0x410fc070u	/* Cortex-A7, low nibble masked */
#define SG_CPUID_MASK		0xfffffff0u

struct sg_symbol {
	const char *name;
	unsigned long addr;
};

static const struct sg_symbol sg_call_targets[] = {
	{ "down_write_trylock", SG_STOCK_TRYLOCK },
	{ "zram_free_page", SG_STOCK_ZRAM_FREE },
	{ "up_write", SG_STOCK_UP_WRITE },
	{ "__down_write_trylock", SG_STOCK_TRYLOCK_INNER },
	{ "__up_write", SG_STOCK_UP_WRITE_INNER },
};

static const struct sg_site sg_spec[SG_SITE_COUNT] = {
	{ "smaps_pte_range", SG_STOCK_SMAPS, SG_SMAPS_OFFSET,
	  SG_SMAPS_WORD, SG_PATCH_WORD, 0 },
	{ "kpageswapn_read", SG_STOCK_KPAGESWAPN, SG_KPAGESWAPN_OFFSET,
	  SG_KPAGESWAPN_WORD, SG_PATCH_WORD, 0 },
	{ "zram_slot_free_notify", SG_STOCK_ZRAM_NOTIFY, 0x00u,
	  0xe1a0c00du, 0xe1a0c00du, 0 },
	{ "zram_slot_free_notify", SG_STOCK_ZRAM_NOTIFY, 0x04u,
	  0xe92dd878u, 0xe92dd878u, 0 },
	{ "zram_slot_free_notify", SG_STOCK_ZRAM_NOTIFY, 0x08u,
	  0xe24cb004u, 0xe24cb004u, 0 },
	{ "zram_slot_free_notify", SG_STOCK_ZRAM_NOTIFY, 0x0cu,
	  0xe52de004u, 0xe52de004u, 0 },
	{ "zram_slot_free_notify", SG_STOCK_ZRAM_NOTIFY, 0x10u,
	  0xebf31b28u, 0xebf31b28u, SG_FTRACE_NOP },
	{ "zram_slot_free_notify", SG_STOCK_ZRAM_NOTIFY, 0x14u,
	  0xe590305cu, 0xe590305cu, 0 },
	{ "zram_slot_free_notify", SG_STOCK_ZRAM_NOTIFY, 0x18u,
	  0xe1a06001u, 0xe1a06001u, 0 },
	{ "zram_slot_free_notify", SG_STOCK_ZRAM_NOTIFY, 0x1cu,
	  0xe5934190u, 0xe5934190u, 0 },
	{ "zram_slot_free_notify", SG_STOCK_ZRAM_NOTIFY, 0x20u,
	  0xe2845008u, 0xe2845008u, 0 },
	{ "zram_slot_free_notify", SG_STOCK_ZRAM_NOTIFY, 0x24u,
	  0xe1a00005u, 0xe1a00005u, 0 },
	{ "zram_slot_free_notify", SG_STOCK_ZRAM_NOTIFY, 0x28u,
	  0xeb11ff16u, 0xebf4ce91u, 0 },
	{ "zram_slot_free_notify", SG_STOCK_ZRAM_NOTIFY, 0x2cu,
	  0xe1a01006u, 0xe3500000u, 0 },
	{ "zram_slot_free_notify", SG_STOCK_ZRAM_NOTIFY, 0x30u,
	  0xe1a00004u, 0x089da878u, 0 },
	{ "zram_slot_free_notify", SG_STOCK_ZRAM_NOTIFY, 0x34u,
	  0xebffffb5u, 0xe1a01006u, 0 },
	{ "zram_slot_free_notify", SG_STOCK_ZRAM_NOTIFY, 0x38u,
	  0xe1a00005u, 0xe1a00004u, 0 },
	{ "zram_slot_free_notify", SG_STOCK_ZRAM_NOTIFY, 0x3cu,
	  0xebf4ce9au, 0xebffffb3u, 0 },
	{ "zram_slot_free_notify", SG_STOCK_ZRAM_NOTIFY, 0x40u,
	  0xe1a00004u, 0xe1a00005u, 0 },
	{ "zram_slot_free_notify", SG_STOCK_ZRAM_NOTIFY, 0x44u,
	  0xe2841070u, 0xebf4ce98u, 0 },
	{ "zram_slot_free_notify", SG_STOCK_ZRAM_NOTIFY, 0x48u,
	  0xebffffa0u, 0xe89da878u, 0 },
	{ "zram_slot_free_notify", SG_STOCK_ZRAM_NOTIFY, 0x4cu,
	  0xe89da878u, 0xe89da878u, 0 },
};

#endif /* SWAP_GUARD_SPEC_H */
