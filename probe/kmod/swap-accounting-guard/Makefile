# Out-of-tree build for the stock NW-A55 kernel (see probe/kmod/mali-r4p0 for
# the same KDIR/CROSS_COMPILE convention).
#
# KBUILD_MODPOST_WARN is deliberately NOT set: an unresolved import must fail
# the build rather than be reported as a warning.
#
#   make          build swap_accounting_guard.ko
#   make verify   full host gate: unit tests, metadata tests, then the build
#   make clean
KDIR ?= $(HOME)/.cache/nw-kernel/linux-kernel-3.10.26
CROSS_COMPILE ?= $(HOME)/.cache/arm-eabi-4.8/bin/arm-eabi-
VMLINUX ?= $(CURDIR)/../../../nwa50-extracted/kernel/vmlinux.bin
KALLSYMS ?= $(CURDIR)/../../../work/android/wdt-nozram-20261012T062210Z/baseline/kallsyms.out

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) modules

check:
	./tests/test-adb-guard.sh
	./tests/test-no-absolute-adb.sh
	./tests/run-tests.sh
	NW_VMLINUX="$(VMLINUX)" NW_KALLSYMS="$(KALLSYMS)" CROSS_COMPILE="$(CROSS_COMPILE)" ./tests/test-notify-arm.sh
	./tests/test-symvers.sh
	./tests/test-observe.sh
	./tests/test-capture.sh
	./tests/test-lean-collect.sh

# Explicit order: build first, then the host suites, then verify the imports of
# the .ko that was actually built. nm output goes to its own file so nm's exit
# status is checked directly instead of being masked by a pipeline.
verify: all
	@$(MAKE) --no-print-directory check
	@NM="$(CROSS_COMPILE)nm"; \
	 "$$NM" -u swap_accounting_guard.ko > .imports.raw; \
	 rc=$$?; \
	 if [ $$rc -ne 0 ]; then echo "nm failed (rc=$$rc)"; rm -f .imports.raw; exit $$rc; fi; \
	 awk '{print $$2}' .imports.raw | sort -u > .imports.tmp; \
	 rm -f .imports.raw; \
	 if [ ! -s .imports.tmp ]; then echo "no imports found in the built .ko"; rm -f .imports.tmp; exit 1; fi; \
	 env -u PYTHONHOME -u PYTHONPATH python3 tools/verify-stock-exports.py \
	     $(VMLINUX) $(KALLSYMS) .imports.tmp --gpl; \
	 rc=$$?; rm -f .imports.tmp; exit $$rc

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) clean
	rm -f tests/test_plan tests/test_txn tests/test_perm tests/test_notify_txn .imports.tmp .imports.raw

.PHONY: all check verify clean
