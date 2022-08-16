#!/bin/ash
# SPDX-License-Identifier: GPL-2.0-only

BiscuitOS-Broiler-default --kernel /mnt/Freeze/BiscuitOS-Broiler/bzImage \
			  --rootfs /mnt/Freeze/BiscuitOS-Broiler/BiscuitOS.img \
			  --memory 256 \
			  --cpu 2 \
			  --cmdline "notsc noapic pci=conf1 reboot=k panic=1 i8042.direct=1 i8042.dumbkbd=1 i8042.nopnp=1 i8042.noaux=1 root=/dev/vda rw rootfstype=ext4 console=ttyS0 loglevel=8"
