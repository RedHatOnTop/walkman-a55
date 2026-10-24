# Out-of-tree build of the station-only mtk_wmt_wifi_soc.ko against Sony's
# prepared 3.10.26 tree. Same KDIR / CROSS_COMPILE convention as probe/kmod/swap-accounting-guard and
# probe/kmod/mali-r4p0. The kernel tree is only read; nothing is cleaned,
# reconfigured or written there (M= builds put every artifact in this directory).
#
#   make            build mtk_wmt_wifi_soc.ko
#   make verify     build, then check vermagic and every undefined symbol
#   make clean
#
# No `| tee` or `| tail` around the build: the shell would report the pipe's
# last exit status instead of make's.
KDIR ?= $(HOME)/.cache/nw-kernel/linux-kernel-3.10.26
CROSS_COMPILE ?= $(HOME)/.cache/arm-eabi-4.8/bin/arm-eabi-
NM := $(CROSS_COMPILE)nm
# Symbols exported by the shipped mtk_stp_wmt_soc.ko (3 of them), in Module.symvers
# format. The kernel's own Module.symvers carries vmlinux exports only. Regenerate
# with `make symvers` if the shipped module ever changes.
WMT_SOC_KO ?= $(CURDIR)/../../../work/device/system/lib/modules/mtk_stp_wmt_soc.ko
EXTRA_SYMVERS := $(CURDIR)/mtk_stp_wmt_soc.symvers
# Shipped modules' vermagic, taken from a module that is known to load on the device.
WANT_VERMAGIC ?= 3.10.26 SMP preempt mod_unload ARMv7

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) \
	    KBUILD_EXTRA_SYMBOLS=$(EXTRA_SYMVERS) modules

# Every undefined symbol must resolve against the stock vmlinux export table
# (kernel Module.symvers, built from the stock kallsyms) or against the shipped
# WMT module's exports. Exit status is checked directly, not through a pipe.
verify: all
	@set -e; \
	t="$$(mktemp -d)"; trap 'rm -rf "$$t"' EXIT; \
	vm="$$(modinfo -F vermagic mtk_wmt_wifi_soc.ko)"; \
	echo "vermagic: $$vm"; \
	if [ "$$vm" != "$(WANT_VERMAGIC) " ] && [ "$$vm" != "$(WANT_VERMAGIC)" ]; then \
	    echo "vermagic mismatch, want '$(WANT_VERMAGIC)'"; exit 1; fi; \
	$(NM) -u mtk_wmt_wifi_soc.ko > "$$t/imports.raw"; \
	awk '{print $$2}' "$$t/imports.raw" | sort -u > "$$t/imports.txt"; \
	test -s "$$t/imports.txt"; \
	awk -F'\t' '{print $$2}' $(KDIR)/Module.symvers | sort -u > "$$t/vmlinux.txt"; \
	awk -F'\t' '{print $$2}' $(EXTRA_SYMVERS) | sort -u > "$$t/wmt.txt"; \
	comm -23 "$$t/imports.txt" "$$t/vmlinux.txt" > "$$t/not-in-vmlinux.txt"; \
	comm -23 "$$t/not-in-vmlinux.txt" "$$t/wmt.txt" > "$$t/unresolved.txt"; \
	n=$$(wc -l < "$$t/imports.txt"); nv=$$(wc -l < "$$t/not-in-vmlinux.txt"); \
	echo "undefined symbols: $$n"; \
	echo "  from vmlinux exports: $$((n - nv))"; \
	echo "  from mtk_stp_wmt_soc: $$(comm -12 "$$t/not-in-vmlinux.txt" "$$t/wmt.txt" | wc -l)"; \
	echo "  unresolved:           $$(wc -l < "$$t/unresolved.txt")"; \
	cat "$$t/unresolved.txt"; \
	test ! -s "$$t/unresolved.txt"

symvers:
	@set -e; \
	t="$$(mktemp -d)"; trap 'rm -rf "$$t"' EXIT; \
	$(NM) $(WMT_SOC_KO) | awk '$$3 ~ /^__ksymtab_/ {sub("__ksymtab_","",$$3); print $$3}' | sort > "$$t/names.txt"; \
	test -s "$$t/names.txt"; \
	awk '{printf "0x00000000\t%s\tmtk_stp_wmt_soc\tEXPORT_SYMBOL\n", $$1}' "$$t/names.txt" > $(EXTRA_SYMVERS)

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) clean

.PHONY: all verify symvers clean
