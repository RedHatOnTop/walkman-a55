# make KDIR=<linux-kernel-3.10.26 after olddefconfig modules_prepare with the
#      stock IKCONFIG> CROSS_COMPILE=<arm-eabi-4.8>/bin/arm-eabi-
obj-m := coreup_mod.o
coreup_mod-y := coreup.o stub.o

KDIR ?= $(HOME)/.cache/nw-kernel/linux-kernel-3.10.26
CROSS_COMPILE ?= $(HOME)/.cache/arm-eabi-4.8/bin/arm-eabi-

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) KBUILD_MODPOST_WARN=1 modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm clean
