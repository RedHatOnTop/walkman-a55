#!/system/bin/sh
#
# Runs inside the container from init as a oneshot service, started from the
# boot action before vold. It is asynchronous: init forks it and carries on to
# class_start, so services may already be starting while it runs. That is safe
# because ueventd is stopped first (see init.rc) and it is the only thing that
# turns uevents into /dev nodes, so nothing can recreate what this removes.
#
# ueventd's coldboot creates device nodes for every block device on the eMMC,
# including the raw partitions. Nothing in this userspace needs them, and the
# project's ROADMAP gates writes to bootimg, system and nvp, so take them away
# rather than leaving them reachable. The nodes the graphics and framework stack
# does need (mali, ion, graphics/fb0, binder, ashmem, trustzone, the sockets)
# are untouched.

for n in mmcblk0 mmcblk0boot0 mmcblk0boot1; do
    rm -f "/dev/$n"
done

# Whatever the kernel enumerated as mmcblk0pN, without assuming how many.
for p in /dev/mmcblk0p*; do
    rm -f "$p"
done

# The by-name tree and the partition links the MTK ueventd rules create.
#
# /dev/trustzone is deliberately NOT removed. libtz_uree.so opens
# /dev/trustzone and /dev/tzmem, and hwcomposer.mt8127.so's BlitManager creates
# a uREE session through it at startup. Measured: with it removed,
# SurfaceFlinger aborts with
#   E/uree.so: UREE_OpenFd open fd fail, err = No such file or directory
#   E/hwcomposer: [BLIT] UREE_CreateSession failed (Non-specific cause.), aborting
# and crash-loops, so nothing boots. It is a TEE interface, not storage, and it
# is not on the project's forbidden list.
rm -rf /dev/block
for n in mbr ebr1 ebr2 ebr3 ebr4 ebr5 ebr6 ebr7 ebr8 ebr9 \
         preloader pro_info protect_f protect_s seccfg uboot \
         bootimg sec_ro logo tee1 tee2 dkb xhrome cm4 nvp var \
         option1 option2 option3 contents bmtpool expdb recovery \
         usrdata cache db android misc-sd nvram pmt; do
    rm -f "/dev/$n"
done

exit 0
