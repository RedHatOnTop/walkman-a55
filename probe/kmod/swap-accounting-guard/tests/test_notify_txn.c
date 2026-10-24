#include "txn_mock.h"

static void case_validation_only(void)
{
	struct mock m;
	struct sg_txn t;
	struct sg_site sites[SG_SITE_COUNT];
	int i, changed = 0, last = SG_SITE_COUNT - 1;

	memcpy(sites, sg_spec, sizeof(sites));
	sites[last].patch_word = sites[last].stock_word;
	for (i = 0; i < SG_SITE_COUNT; i++)
		changed += sites[i].patch_word != sites[i].stock_word;
	reset(&m); m.sites = sites;
	check("validation-only transaction -> OK",
	      sg_run(&ops, &m, sites, addrs, SG_SITE_COUNT, &t), SG_TXN_OK);
	check("only changed words written", t.writes_attempted, changed);
	check("no validation-only stores", m.guard_writes, 0);
	check("no validation-only permission calls", m.guard_permissions, 0);
	check("validation-only never marked patched", t.site[last].patched, 0);
	check("validation-only not made writable", t.site[last].rw_attempted, 0);
	check("writes only inside stopped callback", m.writes_outside_stopped, 0);
}

static int all_stock(const struct mock *m)
{
	int i;

	for (i = 0; i < SG_SITE_COUNT; i++)
		if (m->word[i] != sg_spec[i].stock_word)
			return 0;
	return 1;
}

static void case_stock_guards(void)
{
	struct mock m;
	struct sg_txn t;
	int i;

	reset(&m);
	check("all guarded stock -> OK",
	      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_OK);
	check("stopped guarded words verified", t.guards_verified, 22);
	check("writes required", t.writes_required, 11);
	for (i = 0; i < SG_SITE_COUNT; i++) {
		int writes = sg_spec[i].stock_word != sg_spec[i].patch_word;

		check_at("stopped read verified", i, t.site[i].stopped_read_ok, 1);
		check_at("stopped word", i, t.site[i].stopped_word == sg_spec[i].stock_word, 1);
		check_at("validation-only classification", i, t.site[i].validate_only, !writes);
		check_at("patched classification", i, t.site[i].patched, writes);
		check_at("write permission attempted", i, t.site[i].rw_attempted, writes);
		check_at("RO operation verified", i, t.site[i].ro_verified, writes);
	}
	check("validation-only stores", m.guard_writes, 0);
	check("writes outside stopped", m.writes_outside_stopped, 0);
}

static void case_input_failures(void)
{
	struct mock m;
	struct sg_txn t;
	int i;

	for (i = 0; i < SG_SITE_COUNT; i++) {
		reset(&m); m.word[i] ^= 1u;
		check_at("wrong guarded word", i,
			sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_NOT_STOCK);
		check_at("wrong word writes", i, t.writes_attempted, 0);
		check_at("wrong word permission calls", i, m.rw_calls, 0);
		reset(&m); m.fail_read[i] = 1;
		check_at("unreadable guarded word", i,
			sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_NOT_STOCK);
		check_at("unreadable writes", i, t.writes_attempted, 0);
		check_at("unreadable stopped calls", i, m.stop_calls, 0);
	}
}

static void case_stopped_failures(void)
{
	struct mock m;
	struct sg_txn t;
	int i;

	for (i = 0; i < SG_SITE_COUNT; i++) {
		reset(&m); m.flip_at_stop = i;
		check_at("changed at stop", i,
			sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_REVALIDATE);
		check_at("changed writes", i, t.writes_attempted, 0);
		check_at("initial evidence preserved", i, t.site[i].old_word == sg_spec[i].stock_word, 1);
		check_at("changed stopped evidence", i, t.site[i].stopped_word == (sg_spec[i].stock_word ^ 1u), 1);
		check_at("partial validation count", i, t.guards_verified, SG_SITE_COUNT - 1);
		check_at("changed RO restores", i, t.pages_ro_restored, SG_WRITE_COUNT);
		reset(&m); m.fail_stopped_read[i] = 1;
		check_at("read fails at stop", i,
			sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_REVALIDATE);
		check_at("stopped failure read flag", i, t.site[i].stopped_read_ok, 0);
		check_at("stopped failure writes", i, t.writes_attempted, 0);
	}
}

static void check_failed_write(struct mock *m, struct sg_txn *t, int i, int ordinal)
{
	check_at("write/readback failure", i,
		sg_run(&ops, m, sg_spec, addrs, SG_SITE_COUNT, t), SG_TXN_WRITE);
	check_at("attempted prefix", i, t->writes_attempted, ordinal);
	check_at("rolled back prefix", i, t->rollback_succeeded, ordinal);
	check_at("all original words restored", i, all_stock(m), 1);
	check_at("readonly guard rollback stores", i, m->guard_writes, 0);
	check_at("rollback outside stopped", i, m->writes_outside_stopped, 0);
}

static void case_write_failures(void)
{
	struct mock m;
	struct sg_txn t;
	int i, ordinal = 0;

	for (i = 0; i < SG_SITE_COUNT; i++) {
		if (!sg_site_writes(&sg_spec[i]))
			continue;
		ordinal++;
		reset(&m); m.fail_write[i] = 1;
		check_failed_write(&m, &t, i, ordinal);
		reset(&m); m.fail_readback[i] = 1;
		check_failed_write(&m, &t, i, ordinal);
		reset(&m); m.fail_after_write[i] = 1;
		check_failed_write(&m, &t, i, ordinal);
		check_at("failed readback flag", i, t.site[i].readback_ok, 0);
		reset(&m); m.applied_error[i] = 1;
		check_failed_write(&m, &t, i, ordinal);
		check_at("landed write still read back", i, t.site[i].patched, 1);
	}
}

static void case_permission_failures(void)
{
	struct mock m;
	struct sg_txn t;
	int i, ordinal = 0;

	for (i = 0; i < SG_SITE_COUNT; i++) {
		if (!sg_site_writes(&sg_spec[i]))
			continue;
		ordinal++;
		reset(&m); m.fail_rw[i] = 1;
		check_at("RW refusal", i,
			sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_PERM);
		check_at("RW refusal writes", i, t.writes_attempted, 0);
		check_at("RW attempted restored", i, t.pages_ro_restored, ordinal);
		reset(&m); m.fail_ro[i] = 1;
		check_at("RO failure", i,
			sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_RO_RESTORE);
		check_at("RO failed count", i, t.pages_ro_failed, 1);
		check_at("RO failure complete writes", i, t.writes_succeeded, SG_WRITE_COUNT);
	}
}

static void case_rollback_failures(void)
{
	struct mock m;
	struct sg_txn t;
	int i, ordinal = 0;

	for (i = 0; i < SG_SITE_COUNT; i++) {
		if (!sg_site_writes(&sg_spec[i]))
			continue;
		ordinal++;
		reset(&m); m.fail_write[i] = 1; m.fail_rollback_read[i] = 1;
		check_at("rollback read failure", i,
			sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_ROLLBACK);
		check_at("rollback read error counted", i, t.rollback_failed, 1);
		check_at("rollback read evidence false", i, t.site[i].rollback_read_ok, 0);
		check_at("rollback RO operations", i, t.pages_ro_restored, SG_WRITE_COUNT);
		check_at("no guard rollback writes", i, m.guard_writes, 0);
		reset(&m); m.fail_write[i] = 1; m.fail_rollback_write = 1; m.fail_ro[0] = 1;
		check_at("rollback write plus RO failure", i,
			sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_ROLLBACK);
		check_at("all touched rollback writes attempted", i, t.rollback_attempted, ordinal);
		check_at("all failed rollback writes counted", i, t.rollback_failed, ordinal);
		check_at("RO failure also visible", i, t.pages_ro_failed, 1);
		check_at("no validation-only rollback", i, m.guard_writes, 0);
	}
}

static void case_ftrace_rollback(void)
{
	struct mock m;
	struct sg_txn t;
	int i, index;

	reset(&m);
	index = site_index(0xc03481e8ul);
	if (index < 0) {
		check("mcount rollback guard present", index >= 0, 1);
		return;
	}
	for (i = 0; i < SG_SITE_COUNT; i++) {
		if (!sg_site_writes(&sg_spec[i]))
			continue;
		reset(&m); m.word[index] = SG_FTRACE_NOP; m.fail_write[i] = 1;
		check_at("NOP rollback failure reported", i,
			sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_WRITE);
		check_at("NOP preserved after rollback", i, m.word[index] == SG_FTRACE_NOP, 1);
		check_at("NOP rollback stores", i, m.guard_writes, 0);
		check_at("NOP initial evidence retained", i, t.site[index].old_word == SG_FTRACE_NOP, 1);
	}
}

static void case_ftrace_guard(void)
{
	const unsigned int words[] = { 0xebf31b28u, 0xe8bd4000u };
	struct mock m;
	struct sg_txn t;
	int i, j, index;

	reset(&m);
	index = site_index(0xc03481e8ul);
	check("mcount guard present", index >= 0, 1);
	if (index < 0)
		return;
	for (i = 0; i < 2; i++) {
		for (j = 0; j < 2; j++) {
			reset(&m); m.word[index] = words[i];
			m.replace_at_stop = index; m.stopped_replacement = words[j];
			check("BL/NOP initial and stopped accepted",
			      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_OK);
			check("actual initial mcount word", t.site[index].old_word == words[i], 1);
			check("actual stopped mcount word", t.site[index].stopped_word == words[j], 1);
			check("both mcount states counted", t.guards_verified, SG_SITE_COUNT);
			check("mcount never stored", m.guard_writes, 0);
			check("mcount never restored", t.site[index].rolled_back, 0);
			check("mcount unchanged by guard", m.word[index] == words[j], 1);
		}
		reset(&m); m.word[index] = words[i] ^ 1u;
		check("third mcount value refused",
		      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_NOT_STOCK);
		check("third mcount value zero writes", t.writes_attempted, 0);
		reset(&m); m.replace_at_stop = index; m.stopped_replacement = words[i] ^ 1u;
		check("third stopped mcount value refused",
		      sg_run(&ops, &m, sg_spec, addrs, SG_SITE_COUNT, &t), SG_TXN_REVALIDATE);
		check("third stopped mcount value zero writes", t.writes_attempted, 0);
	}
}

int main(void)
{
	case_ftrace_guard();
	case_ftrace_rollback();
	case_validation_only();
	case_stock_guards();
	case_input_failures();
	case_stopped_failures();
	case_write_failures();
	case_permission_failures();
	case_rollback_failures();
	if (failures) {
		printf("FAILURES: %d\n", failures);
		return 1;
	}
	printf("ALL PASS\n");
	return 0;
}
