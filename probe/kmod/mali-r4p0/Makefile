# Out-of-tree build for the stock NW-A55 kernel (see probe/kmod/coreprobe):
#   make KDIR=<linux-kernel-3.10.26 after olddefconfig modules_prepare>
KDIR ?= $(HOME)/.cache/nw-kernel/linux-kernel-3.10.26
CROSS_COMPILE ?= $(HOME)/.cache/arm-eabi-4.8/bin/arm-eabi-

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) KBUILD_MODPOST_WARN=1 modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm clean
