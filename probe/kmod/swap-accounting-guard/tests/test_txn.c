/*
 * Host tests for the guard transaction (swap_guard_txn.h) - the same code the
 * module runs, driven through struct sg_ops with a mock backend. No device and
 * no kernel tree needed. Stock facts come from swap_guard_spec.h, the same
 * header the module uses.
 */
#include <stdio.h>
#include <string.h>
#include "../swap_guard_txn.h"
#include "../swap_guard_spec.h"

#include "txn_mock.h"

static void case_happy(void)
{
	struct mock m; struct sg_txn t; int r;

	reset(&m);
	r = sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t);
	check("stock -> OK", r, SG_TXN_OK);
	check("stock writes attempted", t.writes_attempted, SG_WRITE_COUNT);
	check("stock writes succeeded", t.writes_succeeded, SG_WRITE_COUNT);
	check("stock permission restores", t.pages_ro_restored, SG_WRITE_COUNT);
	check("stock stage2 ran", t.stage2_ran, 1);
	check("stock patched both",
	      ((int)m.word[0] == (int)PATCH && (int)m.word[1] == (int)PATCH), 1);
	check("stock no RO-time writes", m.writes_while_ro + m.rollback_while_ro, 0);
}

static void case_rejections(void)
{
	struct mock m; struct sg_txn t;

	reset(&m); m.word[1] = 0xdeadbeefu;
	check("second not stock -> NOT_STOCK",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_NOT_STOCK);
	check("second not stock writes == 0", t.writes_attempted, 0);

	reset(&m); m.word[0] = PATCH; m.word[1] = PATCH;
	check("already patched -> NOT_STOCK",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_NOT_STOCK);
	check("already patched writes == 0", t.writes_attempted, 0);

	reset(&m); m.totalswap = 65536;
	check("swap active -> SWAP_ACTIVE",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_SWAP_ACTIVE);
	check("swap active writes == 0", t.writes_attempted, 0);

	reset(&m); m.swap_snapshot_fail = 1;
	check("swap snapshot fail -> SWAP_ACTIVE",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_SWAP_ACTIVE);
}

static void case_stopped(void)
{
	struct mock m; struct sg_txn t;

	reset(&m); m.stop_fails = 1;
	check("stopped fails -> STOPPED",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_STOPPED);
	check("stopped fails writes == 0", t.writes_attempted, 0);
	check("stopped fails ro restored", t.pages_ro_restored, SG_WRITE_COUNT);

	reset(&m); m.stop_skips_fn = 1;
	check("callback never ran -> STOPPED",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_STOPPED);
	check("callback never ran writes == 0", t.writes_attempted, 0);
	check("callback never ran stage2_ran == 0", t.stage2_ran, 0);
	check("callback never ran ro restored", t.pages_ro_restored, SG_WRITE_COUNT);
}

static void case_permissions(void)
{
	struct mock m; struct sg_txn t;

	reset(&m); m.fail_rw[0] = 1;
	check("rw fail site0 -> PERM",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_PERM);
	check("rw fail site0 writes == 0", t.writes_attempted, 0);
	check("rw fail site0 restored attempted page", t.pages_ro_restored, 1);

	reset(&m); m.fail_rw[1] = 1;
	check("rw fail site1 -> PERM",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_PERM);
	check("rw fail site1 restored both attempted", t.pages_ro_restored, 2);

	reset(&m); m.fail_ro[1] = 1;
	check("ro restore fails -> RO_RESTORE",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_RO_RESTORE);
	check("ro restore fails counted", t.pages_ro_failed, 1);
}

static void case_write_and_rollback(void)
{
	struct mock m; struct sg_txn t;

	reset(&m); m.fail_write[0] = 1;
	check("write fail site0 -> WRITE",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_WRITE);
	check("write fail site0 attempted == 1", t.writes_attempted, 1);
	check("write fail site0 rollback ok", t.rollback_succeeded, 1);
	check("write fail site0 no RO-time rollback", m.rollback_while_ro, 0);

	reset(&m); m.fail_write[1] = 1;
	check("write fail site1 -> WRITE",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_WRITE);
	check("write fail site1 rolled back both", t.rollback_succeeded, 2);
	check("write fail site1 site0 stock again", (int)m.word[0], (int)STOCK0);

	reset(&m); m.fail_readback[1] = 1;
	check("readback lie -> WRITE",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_WRITE);
	check("readback lie rolled back both", t.rollback_succeeded, 2);

	reset(&m); m.fail_write[0] = 1; m.fail_rollback_write = 1;
	check("rollback fails -> ROLLBACK",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_ROLLBACK);
	check("rollback attempted all touched", t.rollback_attempted, 1);
	check("rollback failure counted", t.rollback_failed, 1);
}

static void case_perm_then_ro_failure(void)
{
	struct mock m; struct sg_txn t;

	/* a refused permission change AND a page left writable: the worse
	 * state must be reported, not a benign PERM */
	reset(&m); m.fail_rw[1] = 1; m.fail_ro[0] = 1;
	check("perm fail + ro fail -> RO_RESTORE",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_RO_RESTORE);
	check("perm fail + ro fail writes == 0", t.writes_attempted, 0);
}

static void case_rollback_and_ro_failure(void)
{
	struct mock m; struct sg_txn t;

	/* rollback write fails AND a page cannot be restored read-only: the
	 * verdict must be ROLLBACK (worst first), with both counters set */
	reset(&m);
	m.fail_write[1] = 1;		/* the patch write fails at site 1 */
	m.fail_rollback_write = 1;	/* so the rollback write fails too */
	m.fail_ro[0] = 1;		/* and site 0 cannot go read-only */
	check("rollback + ro failure -> ROLLBACK",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_ROLLBACK);
	check("rollback + ro failure rollback_failed", t.rollback_failed >= 1, 1);
	check("rollback + ro failure pages_ro_failed", t.pages_ro_failed >= 1, 1);
	check("rollback + ro failure attempted all touched",
	      t.rollback_attempted >= 1, 1);

	/* permission preparation failure with a page left writable still reports
	 * the read-only failure, not a benign refusal */
	reset(&m); m.fail_rw[1] = 1; m.fail_ro[0] = 1;
	check("perm + ro failure -> RO_RESTORE",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_RO_RESTORE);
	check("perm + ro failure writes == 0", t.writes_attempted, 0);
}

static void case_revalidate_and_args(void)
{
	struct mock m; struct sg_txn t;
	struct sg_ops broken;

	reset(&m); m.flip_second_at_stop = 1;
	check("changed during revalidation -> REVALIDATE",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_REVALIDATE);
	check("changed during revalidation writes == 0", t.writes_attempted, 0);
	check("changed during revalidation ro restored", t.pages_ro_restored, SG_WRITE_COUNT);

	reset(&m);
	check("null ops -> BAD_ARG",
	      sg_run(NULL, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_BAD_ARG);
	check("short n -> BAD_ARG",
	      sg_run(&ops, &m, sg_spec, addrs, 1, &t), SG_TXN_BAD_ARG);
	check("null txn -> BAD_ARG",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, NULL), SG_TXN_BAD_ARG);

	/* every callback is required; a missing one must be rejected, not called */
	broken = ops; broken.write_word = NULL;
	check("missing write_word -> BAD_ARG",
	      sg_run(&broken, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_BAD_ARG);
	broken = ops; broken.stopped = NULL;
	check("missing stopped -> BAD_ARG",
	      sg_run(&broken, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_BAD_ARG);
	broken = ops; broken.set_ro = NULL;
	check("missing set_ro -> BAD_ARG",
	      sg_run(&broken, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_BAD_ARG);
	broken = ops; broken.swap_pages = NULL;
	check("missing swap_pages -> BAD_ARG",
	      sg_run(&broken, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_BAD_ARG);
	broken = ops; broken.read_word = NULL;
	check("missing read_word -> BAD_ARG",
	      sg_run(&broken, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_BAD_ARG);
	broken = ops; broken.set_rw = NULL;
	check("missing set_rw -> BAD_ARG",
	      sg_run(&broken, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_BAD_ARG);
}

static void case_evidence(void)
{
	struct mock m; struct sg_txn t;

	/* a clean run records address, old, new and readback per site */
	reset(&m);
	check("evidence run -> OK",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_OK);
	check("site0 addr recorded", (int)t.site[0].addr, (int)ADDR0);
	check("site0 old recorded", (int)t.site[0].old_word, (int)STOCK0);
	check("site0 new recorded", (int)t.site[0].new_word, (int)PATCH);
	check("site0 readback recorded", (int)t.site[0].readback, (int)PATCH);
	check("site0 old_read_ok", t.site[0].old_read_ok, 1);
	check("site0 readback_ok", t.site[0].readback_ok, 1);
	check("site0 patched", t.site[0].patched, 1);

	/* the readback after a write can fail: not verified, so not patched */
	reset(&m); m.fail_read_after_write = 1;
	check("readback failure -> WRITE",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_WRITE);
	check("readback failure readback_ok == 0", t.site[0].readback_ok, 0);
	check("readback failure patched == 0", t.site[0].patched, 0);

	/* a permission refusal must still report the real observed words */
	reset(&m); m.fail_rw[0] = 1;
	check("perm refusal -> PERM",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_PERM);
	check("perm refusal records addr", (int)t.site[0].addr, (int)ADDR0);
	check("perm refusal records old word", (int)t.site[0].old_word, (int)STOCK0);
	check("perm refusal records read_ok", t.site[0].old_read_ok, 1);
	check("perm refusal rw_attempted", t.site[0].rw_attempted, 1);
	check("perm refusal ro_verified", t.site[0].ro_verified, 1);
	check("perm refusal writes nothing", t.writes_attempted, 0);

	/* a swap refusal likewise keeps the observed words */
	reset(&m); m.totalswap = 4096;
	check("swap refusal -> SWAP_ACTIVE",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_SWAP_ACTIVE);
	check("swap refusal records old word", (int)t.site[0].old_word, (int)STOCK0);
	check("swap refusal records read_ok", t.site[0].old_read_ok, 1);

	/* a backend that fails without writing *out must not leave the recorded
	 * word uninitialised, and must never read as verified */
	reset(&m); m.read_fails_without_writing = 1;
	check("read failure without write -> NOT_STOCK",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_NOT_STOCK);
	check("failed read records word 0", (int)t.site[0].old_word, 0);
	check("failed read records read_ok 0", t.site[0].old_read_ok, 0);
	check("failed read writes nothing", t.writes_attempted, 0);

	/* per-site rollback evidence: value, read_ok and verified flag */
	reset(&m); m.fail_write[1] = 1;
	check("rollback evidence -> WRITE",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_WRITE);
	check("site0 rollback readback recorded", (int)t.site[0].rollback_readback, (int)STOCK0);
	check("site0 rollback write_rc", t.site[0].rollback_write_rc, 0);
	check("site0 rollback read_ok", t.site[0].rollback_read_ok, 1);
	check("site0 rolled_back", t.site[0].rolled_back, 1);
	check("site0 ro_attempted", t.site[0].ro_attempted, 1);
	check("site0 ro_verified", t.site[0].ro_verified, 1);
	check("site0 rw_attempted", t.site[0].rw_attempted, 1);

	/* a rollback whose write fails leaves rollback_ok clear */
	reset(&m); m.fail_write[0] = 1; m.fail_rollback_write = 1;
	check("rollback failure -> ROLLBACK",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_ROLLBACK);
	check("rollback failure write_rc clear", t.site[0].rollback_write_rc == 0, 0);

	/* a refused plan still records the observed words, not zeros */
	reset(&m); m.word[1] = 0xdeadbeefu;
	check("refusal -> NOT_STOCK",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_NOT_STOCK);
	check("refusal records observed word", (int)t.site[0].old_word, (int)STOCK0);
	check("refusal records read_ok", t.site[0].old_read_ok, 1);
	check("refusal writes nothing", t.writes_attempted, 0);

	/* a write that took effect but returned failure is still observed */
	reset(&m); m.write_rc_fail_but_applies = 1;
	check("write rc failure -> WRITE",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_WRITE);
	check("write rc failure recorded rc", t.site[0].write_rc != 0, 1);
	check("write rc failure rolled back", t.rollback_succeeded >= 1, 1);
	check("write rc failure readback seen", (int)t.site[0].readback, (int)PATCH);
}

int main(void)
{
	case_evidence();
	case_happy();
	case_rejections();
	case_stopped();
	case_permissions();
	case_perm_then_ro_failure();
	case_rollback_and_ro_failure();
	case_write_and_rollback();
	case_revalidate_and_args();

	if (failures) { printf("\nFAILURES: %d\n", failures); return 1; }
	printf("\nALL PASS\n");
	return 0;
}
