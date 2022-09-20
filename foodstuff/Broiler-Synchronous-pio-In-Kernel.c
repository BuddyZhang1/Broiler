/*
 * Broiler Synchronous PIO on In-Kernel Device
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

#define BROILER_PIO_PORT	0x6080
#define BROILER_PIO_LEN		0x10

/* Capability */
#define KVM_CAP_BROILER_SYNC_PIO_DEV	250	
/* IOCTL */
#define KVM_CREATE_SYNC_PIO_DEV	_IO(KVMIO, 0xee)

static int Broiler_pio_init(struct broiler *broiler)
{
	if (!kvm_support_extension(broiler, KVM_CAP_BROILER_SYNC_PIO_DEV))
		return 0;

	/* Create Asynchronous IO In-Kernel Device */
	if (ioctl(broiler->vm_fd, KVM_CREATE_SYNC_PIO_DEV))
		return 0;

	return 0;
}
dev_init(Broiler_pio_init);
