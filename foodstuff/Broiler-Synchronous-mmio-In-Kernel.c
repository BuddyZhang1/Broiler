/*
 * Broiler Synchronous MMIO on In-Kernel Device
 *
 * (C) 2022.09.17 BuddyZhang1 <buddy.zhang@aliyun.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "broiler/broiler.h"
#include "broiler/utils.h"
#include "broiler/kvm.h"

#define BROILER_MMIO_BASE	0xD0000020
#define BROILER_MMIO_LEN	0x10

/* Capability */
#define KVM_CAP_BROILER_SYNC_MMIO_DEV	251
/* IOCTL */
#define KVM_CREATE_SYNC_MMIO_DEV	_IO(KVMIO, 0xef)

static int Broiler_mmio_init(struct broiler *broiler)
{
	if (!kvm_support_extension(broiler, KVM_CAP_BROILER_SYNC_MMIO_DEV))
		return 0;

	/* Create Asynchronous MMIO In-Kernel Device */
	if (ioctl(broiler->vm_fd, KVM_CREATE_SYNC_MMIO_DEV))
		return 0;

	return 0;
}
dev_init(Broiler_mmio_init);
