/*
 * swap_guard_txn.h - the guard's transaction, shared between the module and the
 * host tests. All device access goes through struct sg_ops, so the host tests
 * drive this exact code with a mock backend and can count every write attempt.
 *
 * Order:
 *   1. validate ops and arguments
 *   2. read the sites and plan; only specified stock/live words may pass
 *   3. swap snapshot (takes swap_lock, so outside any stopped context)
 *   4. make pages writable, tracking every page whose change was ATTEMPTED
 *   5. one stopped context (single writer): revalidate, then write and read
 *      back each site, rolling back in place on failure
 *   6. restore read-only outside the stopped context, on every path
 *
 * Every site records its address, the word found, the word written, what was
 * read back and whether each step succeeded, so the caller can log evidence
 * instead of counts. Permission changes are verified by page-table inspection
 * in sg_ops, never by writing text.
 */
#ifndef SWAP_GUARD_TXN_H
#define SWAP_GUARD_TXN_H

#include "swap_guard_plan.h"

struct sg_ops {
	/* 0 on success */
	int (*read_word)(void *ctx, unsigned long addr, unsigned int *out);
	/* store + instruction-cache maintenance only. The transaction owns the
	 * readback, so a write that took effect but returned failure is still
	 * observed. 0 on success. */
	int (*write_word)(void *ctx, unsigned long addr, unsigned int word);
	/* make one page writable / read-only, verified by page-table inspection */
	int (*set_rw)(void *ctx, unsigned long addr);
	int (*set_ro)(void *ctx, unsigned long addr);
	/* run fn(data) on exactly one CPU with the others stopped; 0 if it ran */
	int (*stopped)(void *ctx, int (*fn)(void *), void *data);
	/* 0 on success, *totalswap set to the swap page count */
	int (*swap_pages)(void *ctx, unsigned long *totalswap);
};

enum sg_txn_result {
	SG_TXN_OK = 0,
	SG_TXN_BAD_ARG = -1,
	SG_TXN_NOT_STOCK = -2,
	SG_TXN_SWAP_ACTIVE = -3,
	SG_TXN_PERM = -4,
	SG_TXN_STOPPED = -5,
	SG_TXN_WRITE = -6,
	SG_TXN_REVALIDATE = -7,
	SG_TXN_ROLLBACK = -8,
	SG_TXN_RO_RESTORE = -9,
};

/* Per-site evidence, filled during the transaction. */
struct sg_site_result {
	unsigned long addr;
	unsigned int old_word;
	unsigned int new_word;
	unsigned int readback;
	int old_read_ok;
	unsigned int stopped_word;
	int stopped_read_ok;
	int validate_only;
	int write_rc;
	int readback_ok;
	int patched;		/* readback == new_word */
	unsigned int rollback_readback;	/* word read back after the rollback write */
	int rollback_write_rc;	/* what the rollback write returned */
	int rollback_read_ok;	/* whether that readback succeeded */
	int rolled_back;	/* rollback_readback == old_word */
	int rw_attempted;	/* this site's page was made writable */
	int ro_attempted;	/* this site's page had set_ro called */
	int ro_verified;	/* set_ro reported success */
};

struct sg_txn {
	struct sg_site_result site[SG_SITE_COUNT];
	int writes_attempted;
	int writes_succeeded;
	int rollback_attempted;
	int rollback_succeeded;
	int rollback_failed;
	int pages_rw_attempted;
	int pages_ro_restored;
	int pages_ro_failed;
	int stopped_calls;
	int stage2_ran;
	int guards_verified;
	int writes_required;
};

struct sg_txn_ctx {
	const struct sg_ops *ops;
	void *ctx;
	const struct sg_site *sites;
	const unsigned long *addrs;
	int n;
	int rw_attempted[SG_SITE_COUNT];
	int attempted[SG_SITE_COUNT];
	int stage2_ran;
	int stage2_rc;
	struct sg_txn *txn;
};

static inline int sg_ops_complete(const struct sg_ops *o)
{
	return o && o->read_word && o->write_word && o->set_rw && o->set_ro &&
	       o->stopped && o->swap_pages;
}

static inline void sg_txn_reset(struct sg_txn *t)
{
	int i;

	for (i = 0; i < SG_SITE_COUNT; i++) {
		t->site[i].addr = 0;
		t->site[i].old_word = 0;
		t->site[i].new_word = 0;
		t->site[i].readback = 0;
		t->site[i].old_read_ok = 0;
		t->site[i].stopped_word = 0;
		t->site[i].stopped_read_ok = 0;
		t->site[i].validate_only = 0;
		t->site[i].write_rc = 0;
		t->site[i].readback_ok = 0;
		t->site[i].patched = 0;
		t->site[i].rollback_readback = 0;
		t->site[i].rollback_write_rc = 0;
		t->site[i].rollback_read_ok = 0;
		t->site[i].rolled_back = 0;
		t->site[i].rw_attempted = 0;
		t->site[i].ro_attempted = 0;
		t->site[i].ro_verified = 0;
	}
	t->writes_attempted = 0; t->writes_succeeded = 0;
	t->rollback_attempted = 0; t->rollback_succeeded = 0;
	t->rollback_failed = 0; t->pages_rw_attempted = 0;
	t->pages_ro_restored = 0; t->pages_ro_failed = 0;
	t->stopped_calls = 0; t->stage2_ran = 0;
	t->guards_verified = 0; t->writes_required = 0;
}

static inline int sg_read_all(struct sg_txn_ctx *c, struct sg_site_state *st,
			      int *need)
{
	int i;

	for (i = 0; i < c->n; i++) {
		/* Initialise before the read: a backend that fails without
		 * writing *out must not leave the word uninitialised, because the
		 * refusal paths record it. read_ok stays 0, so it is never
		 * treated as verified. */
		st[i].word = 0;
		st[i].read_ok = (c->ops->read_word(c->ctx, c->addrs[i],
						   &st[i].word) == 0);
	}

	return sg_plan(c->sites, st, c->n, need);
}

/* Restore every page whose permission change was attempted, including the one
 * that failed. Returns 0 only if all of them are read-only afterwards. */
static inline int sg_restore_ro(struct sg_txn_ctx *c)
{
	int i, rc = 0;

	for (i = 0; i < c->n; i++) {
		if (!c->rw_attempted[i])
			continue;
		c->txn->site[i].ro_attempted = 1;
		if (c->ops->set_ro(c->ctx, c->addrs[i]) == 0) {
			c->txn->site[i].ro_verified = 1;
			c->txn->pages_ro_restored++;
		} else {
			c->txn->pages_ro_failed++;
			rc = -1;
		}
	}

	return rc;
}

/* Write one site and read it back, recording the evidence either way: the
 * readback happens even when the write reported failure, because a write can
 * take effect and still return an error. */
static inline void sg_site_write(struct sg_txn_ctx *c, int i)
{
	struct sg_site_result *res = &c->txn->site[i];

	res->addr = c->addrs[i];
	res->old_word = c->sites[i].stock_word;
	res->new_word = c->sites[i].patch_word;
	c->attempted[i] = 1;
	c->txn->writes_attempted++;
	res->write_rc = c->ops->write_word(c->ctx, c->addrs[i], res->new_word);
	res->readback_ok = (c->ops->read_word(c->ctx, c->addrs[i],
					      &res->readback) == 0);
	res->patched = res->readback_ok && res->readback == res->new_word;
	if (res->write_rc == 0 && res->patched)
		c->txn->writes_succeeded++;
}

/* Roll back every touched site while still writable and still inside the single
 * writer. Does not stop at the first failure. */
static inline int sg_rollback_inplace(struct sg_txn_ctx *c)
{
	int i, rc = 0;

	for (i = 0; i < c->n; i++) {
		struct sg_site_result *res = &c->txn->site[i];
		unsigned int rb = 0;

		if (!c->attempted[i])
			continue;
		c->txn->rollback_attempted++;
		res->rollback_write_rc =
			c->ops->write_word(c->ctx, c->addrs[i], res->old_word);
		res->rollback_read_ok =
			(c->ops->read_word(c->ctx, c->addrs[i], &rb) == 0);
		if (res->rollback_read_ok)
			res->rollback_readback = rb;
		if (res->rollback_write_rc != 0 || !res->rollback_read_ok ||
		    res->rollback_readback != res->old_word) {
			c->txn->rollback_failed++;
			rc = -1;
			continue;
		}
		res->rolled_back = 1;
		c->txn->rollback_succeeded++;
	}

	return rc;
}

/* Runs inside the stopped context. No printk, no allocation, no locking. */
static inline int sg_stage2(void *data)
{
	struct sg_txn_ctx *c = data;
	struct sg_site_state st[SG_SITE_COUNT];
	int i, need = 0, plan;

	c->stage2_ran = 1;

	plan = sg_read_all(c, st, &need);
	for (i = 0; i < c->n; i++) {
		c->txn->site[i].stopped_word = st[i].word;
		c->txn->site[i].stopped_read_ok = st[i].read_ok;
		if (st[i].read_ok && sg_site_matches(&c->sites[i], st[i].word))
			c->txn->guards_verified++;
	}
	if (plan != SG_PLAN_PATCH) {
		c->stage2_rc = SG_TXN_REVALIDATE;
		return 0;
	}

	for (i = 0; i < c->n; i++) {
		if (!sg_site_writes(&c->sites[i]))
			continue;
		sg_site_write(c, i);
		/* an adapter that reported failure is not trusted even if the
		 * store appears to have landed: roll back and report */
		if (c->txn->site[i].write_rc != 0 || !c->txn->site[i].patched) {
			c->stage2_rc = SG_TXN_WRITE;
			sg_rollback_inplace(c);
			return 0;
		}
	}

	c->stage2_rc = SG_TXN_OK;
	return 0;
}

/* Worst news first. */
static inline int sg_txn_verdict(const struct sg_txn_ctx *c, int ro_rc, int stop_rc)
{
	if (c->txn->rollback_failed)
		return SG_TXN_ROLLBACK;
	if (c->txn->pages_ro_failed)
		return SG_TXN_RO_RESTORE;
	if (stop_rc != 0 || !c->stage2_ran)
		return SG_TXN_STOPPED;
	if (ro_rc != 0)
		return SG_TXN_RO_RESTORE;
	return c->stage2_rc;
}

/* Steps 1-3: plan and the swap snapshot. 0 when it is safe to continue. */
static inline int sg_prepare(struct sg_txn_ctx *c)
{
	struct sg_site_state st[SG_SITE_COUNT];
	unsigned long totalswap = 0;
	int i, r, need = 0;

	for (i = 0; i < c->n; i++) {
		c->rw_attempted[i] = 0;
		c->attempted[i] = 0;
	}
	c->stage2_ran = 0;
	c->stage2_rc = SG_TXN_OK;

	r = sg_read_all(c, st, &need);
	/* Record what the initial read actually observed before any preflight
	 * result is returned, so a permission or swap refusal still reports the
	 * real address, word and read status instead of zeros. */
	for (i = 0; i < c->n; i++) {
		c->txn->site[i].addr = c->addrs[i];
		c->txn->site[i].old_word = st[i].word;
		c->txn->site[i].old_read_ok = st[i].read_ok;
		c->txn->site[i].new_word = sg_site_writes(&c->sites[i]) ?
			c->sites[i].patch_word : st[i].word;
		c->txn->site[i].validate_only = !sg_site_writes(&c->sites[i]);
	}
	if (r != SG_PLAN_PATCH)
		return SG_TXN_NOT_STOCK;
	c->txn->writes_required = need;
	if (c->ops->swap_pages(c->ctx, &totalswap) != 0 || totalswap != 0)
		return SG_TXN_SWAP_ACTIVE;

	return 0;
}

/* Step 4: make the pages writable, tracking every attempted page. */
static inline int sg_make_writable(struct sg_txn_ctx *c)
{
	int i;

	for (i = 0; i < c->n; i++) {
		if (!sg_site_writes(&c->sites[i]))
			continue;
		c->rw_attempted[i] = 1;
		c->txn->site[i].rw_attempted = 1;
		c->txn->pages_rw_attempted++;
		if (c->ops->set_rw(c->ctx, c->addrs[i]) != 0)
			return -1;
	}

	return 0;
}

static inline int sg_run(const struct sg_ops *ops, void *ctx,
			 const struct sg_site *sites, const unsigned long *addrs,
			 int n, struct sg_txn *txn)
{
	struct sg_txn_ctx c;
	int stop_rc = 0, ro_rc, perm_failed, r;

	if (!sg_ops_complete(ops) || !sites || !addrs || !txn || n != SG_SITE_COUNT)
		return SG_TXN_BAD_ARG;

	sg_txn_reset(txn);
	c.ops = ops; c.ctx = ctx; c.sites = sites; c.addrs = addrs; c.n = n;
	c.txn = txn;

	r = sg_prepare(&c);
	if (r)
		return r;

	perm_failed = sg_make_writable(&c);
	if (!perm_failed) {
		txn->stopped_calls++;
		stop_rc = ops->stopped(ctx, sg_stage2, &c);
		txn->stage2_ran = c.stage2_ran;
	}

	ro_rc = sg_restore_ro(&c);

	/* A page left writable outranks a refused permission change, but a failed
	 * rollback still outranks both: sg_txn_verdict orders them, so the normal
	 * post-stopped path goes through it. Only a permission preparation that
	 * never reached the stopped context short-circuits here. */
	if (perm_failed) {
		if (txn->pages_ro_failed)
			return SG_TXN_RO_RESTORE;
		return SG_TXN_PERM;
	}

	return sg_txn_verdict(&c, ro_rc, stop_rc);
}

#endif /* SWAP_GUARD_TXN_H */
