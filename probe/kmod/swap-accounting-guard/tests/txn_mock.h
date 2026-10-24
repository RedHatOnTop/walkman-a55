#ifndef TXN_MOCK_H
#define TXN_MOCK_H

#include <stdio.h>
#include <string.h>
#include "../swap_guard_txn.h"
#include "../swap_guard_spec.h"

static int failures;

#define STOCK0 SG_SMAPS_WORD
#define STOCK1 SG_KPAGESWAPN_WORD
#define PATCH  SG_PATCH_WORD
#define ADDR0  0xc01acb90ul
#define ADDR1  0xc01b99a4ul

static unsigned long addrs[SG_SITE_COUNT];

struct mock {
	const struct sg_site *sites;
	int guard_writes, guard_permissions, writes_outside_stopped;
	int in_stopped, flip_at_stop;
	int replace_at_stop;
	unsigned int stopped_replacement;
	int fail_stopped_read[SG_SITE_COUNT];
	int fail_after_write[SG_SITE_COUNT];
	int applied_error[SG_SITE_COUNT];
	int rollback_seen[SG_SITE_COUNT];
	int fail_rollback_read[SG_SITE_COUNT];
	unsigned int word[SG_SITE_COUNT];
	int rw[SG_SITE_COUNT];		/* current permission state */
	int fail_read[SG_SITE_COUNT];
	int fail_write[SG_SITE_COUNT];
	int fail_readback[SG_SITE_COUNT];
	int fail_rw[SG_SITE_COUNT];
	int fail_ro[SG_SITE_COUNT];
	int fail_rollback_write;
	int stop_fails;
	int stop_skips_fn;
	int flip_second_at_stop;
	unsigned long totalswap;
	int swap_snapshot_fail;
	int rw_calls, ro_calls, stop_calls;
	int fail_read_after_write;
	int read_fails_without_writing;	/* returns -1, leaves *out untouched */
	int write_rc_fail_but_applies;
	int writes_while_ro;		/* patch writes attempted with pages RO */
	int rollback_while_ro;		/* rollback writes with pages RO */
};

static int site_index(unsigned long addr)
{
	int i;

	for (i = 0; i < SG_SITE_COUNT; i++)
		if (addrs[i] == addr)
			return i;
	return -1;
}

static int m_read(void *ctx, unsigned long addr, unsigned int *out)
{
	struct mock *m = ctx;
	int i = site_index(addr);

	if (i < 0)
		return -1;

	if ((m->in_stopped && m->fail_stopped_read[i]) ||
	    (m->rollback_seen[i] && m->fail_rollback_read[i]))
		return -1;
	if (m->read_fails_without_writing)
		return -1;	/* deliberately does not touch *out */
	if (m->fail_read[i])
		return -1;
	if ((m->fail_read_after_write || m->fail_after_write[i]) && m->sites[i].stock_word != m->sites[i].patch_word &&
	    m->word[i] == m->sites[i].patch_word)
		return -1;
	if (m->sites[i].stock_word != m->sites[i].patch_word &&
	    m->word[i] == m->sites[i].patch_word && m->fail_readback[i]) {
		*out = 0xdeadbeefu;
		return 0;
	}
	*out = m->word[i];
	return 0;
}

static int m_write(void *ctx, unsigned long addr, unsigned int word)
{
	struct mock *m = ctx;
	int i = site_index(addr);

	if (i < 0)
		return -1;

	if (m->sites[i].stock_word == m->sites[i].patch_word) {
		m->guard_writes++;
		return -1;
	}
	if (!m->in_stopped)
		m->writes_outside_stopped++;
	if (!m->rw[i]) {
		if (word == m->sites[i].patch_word)
			m->writes_while_ro++;
		else
			m->rollback_while_ro++;
		return -1;
	}
	if (word == m->sites[i].stock_word)
		m->rollback_seen[i] = 1;
	if (m->fail_write[i] && word == m->sites[i].patch_word)
		return -1;
	if (m->fail_rollback_write && word != m->sites[i].patch_word)
		return -1;
	m->word[i] = word;
	if (word != m->sites[i].patch_word)
		m->fail_readback[i] = 0;
	if ((m->write_rc_fail_but_applies || m->applied_error[i]) && word == m->sites[i].patch_word)
		return -1;	/* the store landed, the return says otherwise */
	return 0;
}

static int m_set_rw(void *ctx, unsigned long addr)
{
	struct mock *m = ctx;
	int i = site_index(addr);

	if (i < 0)
		return -1;

	if (m->sites[i].stock_word == m->sites[i].patch_word)
		m->guard_permissions++;
	m->rw_calls++;
	if (m->fail_rw[i])
		return -1;
	{
		int j;

		for (j = 0; j < SG_SITE_COUNT; j++)
			if ((addrs[j] >> 12) == (addr >> 12))
				m->rw[j] = 1;
	}
	return 0;
}

static int m_set_ro(void *ctx, unsigned long addr)
{
	struct mock *m = ctx;
	int i = site_index(addr);

	if (i < 0)
		return -1;

	if (m->sites[i].stock_word == m->sites[i].patch_word)
		m->guard_permissions++;
	m->ro_calls++;
	if (m->fail_ro[i])
		return -1;
	{
		int j;

		for (j = 0; j < SG_SITE_COUNT; j++)
			if ((addrs[j] >> 12) == (addr >> 12))
				m->rw[j] = 0;
	}
	return 0;
}

static int m_stopped(void *ctx, int (*fn)(void *), void *data)
{
	struct mock *m = ctx;
	int r;

	m->stop_calls++;
	if (m->stop_fails)
		return -1;
	if (m->stop_skips_fn)
		return 0;		/* "succeeded" but the callback never ran */
	if (m->flip_second_at_stop)
		m->word[1] = 0xdeadbeefu;
	if (m->flip_at_stop >= 0)
		m->word[m->flip_at_stop] ^= 1u;
	if (m->replace_at_stop >= 0)
		m->word[m->replace_at_stop] = m->stopped_replacement;
	m->in_stopped = 1;
	r = fn(data);
	m->in_stopped = 0;
	return r;
}

static int m_swap(void *ctx, unsigned long *totalswap)
{
	struct mock *m = ctx;

	if (m->swap_snapshot_fail)
		return -1;
	*totalswap = m->totalswap;
	return 0;
}

static const struct sg_ops ops = {
	m_read, m_write, m_set_rw, m_set_ro, m_stopped, m_swap,
};

static void reset(struct mock *m)
{
	int i;

	memset(m, 0, sizeof(*m));
	m->sites = sg_spec;
	m->flip_at_stop = -1;
	m->replace_at_stop = -1;
	for (i = 0; i < SG_SITE_COUNT; i++) {
		addrs[i] = sg_spec[i].expect_addr + sg_spec[i].offset;
		m->word[i] = sg_spec[i].stock_word;
	}
}

static void check(const char *name, int got, int want)
{
	if (got == want) {
		printf("PASS %-50s (%d)\n", name, got);
	} else {
		printf("FAIL %-50s got=%d want=%d\n", name, got, want);
		failures++;
	}
}

static inline void check_at(const char *name, int i, int got, int want)
{
	char label[96];

	snprintf(label, sizeof(label), "%s[%d]", name, i);
	check(label, got, want);
}

#endif
