#!/bin/ash
# SPDX-License-Identifier: GPL-2.0-only

BiscuitOS-Broiler-default --kernel /mnt/Freeze/BiscuitOS-Broiler/bzImage \
			  --rootfs /mnt/Freeze/BiscuitOS-Broiler/BiscuitOS.img \
			  --memory 1024 \
			  --cpu 1 \
			  --cmdline "noapic noacpi pci=conf1 reboot=k panic=1 i8042.direct=1 i8042.dumbkbd=1 i8042.nopnp=1 earlyprintk=serial i8042.noaux=1 console=ttyS0 loglevel=0"
