#!/bin/sh
# Host tests for the guard: pure planner and the shared transaction. No device
# and no kernel tree needed. Warnings are errors.
set -eu
cd "$(dirname "$0")/.."
. ./tests/adb-guard.sh
CC="${CC:-gcc}"
CFLAGS="-Wall -Wextra -Werror -O1"
$CC $CFLAGS -o tests/test_plan tests/test_plan.c
$CC $CFLAGS -o tests/test_txn tests/test_txn.c
$CC $CFLAGS -o tests/test_perm tests/test_perm.c
$CC $CFLAGS -o tests/test_notify_txn tests/test_notify_txn.c
./tests/test_plan
./tests/test_txn
./tests/test_perm
./tests/test_notify_txn
