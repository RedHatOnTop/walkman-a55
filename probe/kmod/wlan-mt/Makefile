# The three Wi-Fi modules tools/android-boot.sh loads besides the WMT chardev:
# wlan_mt_bp.ko, the MT8590 CONSYS Wi-Fi driver from Sony's GPL tree, and the
# compat_bp/cfg80211_bp.ko it runs on, from backports 3.17.1-1 configured the
# way the WIFITEST bundle's were (work/wifitest/bundle/WIFITEST/BUILD.md).
# The bundle is no longer needed. When it is there, `verify` checks that the
# compat and cfg80211 built here have its code byte for byte; their strings
# differ only in the build directory.
#
# The backports tree is copied out of KDIR into BUILD and patched there; KDIR
# is only read. Backports regenerates its Kconfig from KDIR's .config, which
# has no WIRELESS_EXT, so CFG80211_WEXT drops out exactly as in the bundle.
#
#   make            build $(BUILD)/{wlan_mt,compat,cfg80211}_bp.ko
#   make verify     build, then check vermagic, imports and the bundle match
#   make clean      remove BUILD
#
# Needs probe/kmod/mtk-wmt-wifi-soc built first: wlan_mt imports its exports.
# No `| tee` or `| tail` around the build: the shell would report the pipe's
# last exit status instead of make's.
KDIR ?= $(HOME)/.cache/nw-kernel/linux-kernel-3.10.26
CROSS_COMPILE ?= $(HOME)/.cache/arm-eabi-4.8/bin/arm-eabi-
BUILD ?= $(abspath $(CURDIR)/../../../work/wlan-mt)
WIFI_BUNDLE ?= $(abspath $(CURDIR)/../../../work/wifitest/bundle/WIFITEST)
STOCK_CONFIG ?= $(abspath $(CURDIR)/../../../nwa50-extracted/kernel/kconfig.txt)
WMT_DIR := $(abspath $(CURDIR)/../mtk-wmt-wifi-soc)
NM := $(CROSS_COMPILE)nm
OBJCOPY := $(CROSS_COMPILE)objcopy
STRIP := $(CROSS_COMPILE)strip
WANT_VERMAGIC ?= 3.10.26 SMP preempt mod_unload ARMv7

BP := $(BUILD)/bp
WLAN := $(BP)/drivers/net/wireless/mt_soc/conn_soc/mt_wifi/wlan
PATCHES := $(sort $(wildcard $(CURDIR)/patches/*.patch))

# Recursive, so $(BP)/versions is read once the tree has been copied.
bpver = $(shell sed -n 's/^$(1)="\(.*\)"$$/\1/p' $(BP)/versions)
NOSTD = -I$(BP)/backport-include/ -I$(BP)/backport-include/uapi \
	-I$(BP)/include/ -I$(BP)/include/uapi \
	-include $(BP)/backport-include/backport/backport.h \
	-DBACKPORTS_VERSION=\"$(call bpver,BACKPORTS_VERSION)\" \
	-DBACKPORTED_KERNEL_VERSION=\"$(call bpver,BACKPORTED_KERNEL_VERSION)\" \
	-DBACKPORTED_KERNEL_NAME=\"$(call bpver,BACKPORTED_KERNEL_NAME)\"

MODULES := $(BUILD)/wlan_mt_bp.ko $(BUILD)/compat_bp.ko $(BUILD)/cfg80211_bp.ko

all: $(MODULES)

# KDIR must be prepared with the stock IKCONFIG. NR_CPUS alone moves
# task_struct fields and the WORK_CPU_UNBOUND constant: a build against
# NR_CPUS=4 oopsed in the first probe on the device. auto.conf is what
# modules_prepare generated, so it catches a .config edited after that too.
stockconfig:
	@set -e; \
	t="$$(mktemp -d)"; trap 'rm -rf "$$t"' EXIT; \
	grep '^CONFIG_' $(STOCK_CONFIG) | sort > "$$t/stock"; \
	grep '^CONFIG_' $(KDIR)/include/config/auto.conf | sort > "$$t/kdir"; \
	diff "$$t/stock" "$$t/kdir" \
	    || { echo "$(KDIR) is not prepared with the stock config ($(STOCK_CONFIG))"; exit 1; }

# git apply outside any repository: the ceiling keeps it from finding this one.
$(BP)/.patched: $(PATCHES)
	test ! -e $(BP)
	mkdir -p $(BUILD)
	cp -a $(KDIR)/backports-3.17.1-1 $(BP)
	cd $(BP) && for p in $(PATCHES); do \
	    GIT_CEILING_DIRECTORIES=$(BUILD) git apply -p1 $$p || exit 1; done
	touch $@

$(BP)/Module.symvers: $(BP)/.patched | stockconfig
	$(MAKE) -C $(BP) KLIB=$(KDIR) KLIB_BUILD=$(KDIR) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE)

$(BUILD)/wlan_mt_bp.ko: $(BP)/Module.symvers | stockconfig
	test -f $(WMT_DIR)/Module.symvers || { echo "build probe/kmod/mtk-wmt-wifi-soc first"; exit 1; }
	$(MAKE) -C $(KDIR) M=$(WLAN) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) \
	    NOSTDINC_FLAGS='$(NOSTD)' \
	    KBUILD_EXTRA_SYMBOLS="$(BP)/Module.symvers $(WMT_DIR)/Module.symvers" \
	    KCFLAGS="-DCONFIG_NL80211_TESTMODE=1 -DCONFIG_WEXT_PRIV=1" \
	    CONFIG_MTK_CONNSYS_WIFI=m CONFIG_MTK_WLAN_SUPPORT=y CONFIG_MTK_WLAN_CHIP=CONSYS modules
	$(STRIP) --strip-debug -o $@ $(WLAN)/wlan_mt.ko

$(BUILD)/compat_bp.ko: $(BP)/Module.symvers
	$(STRIP) --strip-debug -o $@ $(BP)/compat/compat.ko

$(BUILD)/cfg80211_bp.ko: $(BP)/Module.symvers
	$(STRIP) --strip-debug -o $@ $(BP)/net/wireless/cfg80211.ko

# Every undefined symbol must resolve against the stock vmlinux exports, the
# backports modules or the WMT modules.
verify: all
	@set -e; \
	t="$$(mktemp -d)"; trap 'rm -rf "$$t"' EXIT; \
	cat $(KDIR)/Module.symvers $(BP)/Module.symvers $(WMT_DIR)/Module.symvers \
	    | awk -F'\t' '{print $$2}' | sort -u > "$$t/exports.txt"; \
	for ko in $(MODULES); do \
	    vm="$$(modinfo -F vermagic $$ko)"; \
	    if [ "$$vm" != "$(WANT_VERMAGIC) " ] && [ "$$vm" != "$(WANT_VERMAGIC)" ]; then \
	        echo "$$ko: vermagic '$$vm', want '$(WANT_VERMAGIC)'"; exit 1; fi; \
	    $(NM) -u $$ko | awk '{print $$2}' | sort -u > "$$t/imports.txt"; \
	    test -s "$$t/imports.txt"; \
	    comm -23 "$$t/imports.txt" "$$t/exports.txt" > "$$t/unresolved.txt"; \
	    echo "$${ko##*/}: undefined symbols $$(wc -l < "$$t/imports.txt"), unresolved $$(wc -l < "$$t/unresolved.txt")"; \
	    cat "$$t/unresolved.txt"; \
	    test ! -s "$$t/unresolved.txt"; \
	done; \
	if [ ! -d $(WIFI_BUNDLE)/modules ]; then echo "no WIFITEST bundle to compare with"; exit 0; fi; \
	for m in compat cfg80211; do \
	    for s in .text .init.text .exit.text __ksymtab __ksymtab_gpl; do \
	        a="$$($(OBJCOPY) -O binary --only-section=$$s $(BUILD)/$${m}_bp.ko /dev/stdout | md5sum)"; \
	        b="$$($(OBJCOPY) -O binary --only-section=$$s $(WIFI_BUNDLE)/modules/$${m}_bp.ko /dev/stdout | md5sum)"; \
	        test "$$a" = "$$b" || { echo "$${m}_bp $$s differs from the bundle's"; exit 1; }; \
	    done; \
	    echo "$${m}_bp: code and exports are the bundle's"; \
	done

clean:
	rm -rf $(BUILD)

.PHONY: all stockconfig verify clean
