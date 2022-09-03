/*
 * Broiler PCI MSI Interrupt
 *
 * (C) 2022.08.08 BuddyZhang1 <buddy.zhang@aliyun.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "broiler/broiler.h"
#include "broiler/utils.h"
#include "broiler/pci.h"
#include "broiler/device.h"
#include "broiler/ioport.h"
#include "broiler/ioeventfd.h"
#include "broiler/kvm.h"
#include <sys/eventfd.h>

/* Doorball */
#define DOORBALL_REG	0x10

static pthread_t doorball_thread;
static int doorball_efd = 0;
static struct pci_device Broiler_pci_device;
static struct device Broiler_device = {
	.bus_type	= DEVICE_BUS_PCI,
	.data		= &Broiler_pci_device,
};

static void Broiler_pci_bar_callback(struct broiler_cpu *vcpu,
		u64 addr, u8 *data, u32 len, u8 is_write, void *ptr) { }

static void doorball_msi_init(struct broiler *broiler, struct pci_device *pdev)
{
	pdev->msi.cap = PCI_CAP_ID_MSI;
	pdev->msi.next = 0;
	pdev->msi.ctrl = 0; /* 1-Entry */
	/* Legacy hack: ignore writes to uninit regions (e.g. ROM BAR) */
	pdev->msi.msi_cap0.msg_addr_lo = 0xFF;
	pdev->msi.msi_cap0.msg_data    = 0xFF;
}

static void doorball_msi_raise(struct broiler *broiler, struct pci_device *pdev)
{
	struct kvm_msi msi = {
		.address_lo = pdev->msi.msi_cap0.msg_addr_lo,
		.address_hi = 0x00,
		.data       = pdev->msi.msi_cap0.msg_data,
	};
	syscall(600, 1);
	irq_signal_msi(broiler, &msi);
	syscall(600, 0);
}

static void *doorball_thdhands(void *dev)
{
	struct broiler *broiler = dev;
	struct kvm_msi msi;
	u64 data;
	int r;

	while (1) {
		r = read(doorball_efd, &data, sizeof(u64));
		if (r < 0)
			continue;

		/* Emulate Asynchronous IO */
		sleep(2);

		/* Injuect MSI/MSIX Interrupt */
		doorball_msi_raise(broiler, &Broiler_pci_device);
	}
	pthread_exit(NULL);

	return NULL;
}

static int doorball_io_init(struct broiler *broiler, struct pci_device *pdev)
{
	u32 io_addr = pci_bar_address(pdev, 0);
	struct kvm_ioeventfd kvm_ioevent;
	int r;

	doorball_efd = eventfd(0, 0);
	if (doorball_efd < 0) {
		r = -errno;
		goto err_efd;
	}

	/* Asynchronous doorball thread */
	if (pthread_create(&doorball_thread, NULL,
					doorball_thdhands, broiler)) {
		r = -errno;
		goto err_pth;
	}

	/* KVM Asynchronous MMIO */
	kvm_ioevent = (struct kvm_ioeventfd) {
		.addr 	= io_addr + DOORBALL_REG,
		.len	= sizeof(u16),
		.fd	= doorball_efd,
		.flags	= KVM_IOEVENTFD_FLAG_PIO,
	};

	r = ioctl(broiler->vm_fd, KVM_IOEVENTFD, &kvm_ioevent);
	if (r) {
		r = -errno;
		goto err_ioctl;
	}

	return 0;

err_ioctl:
	pthread_kill(doorball_thread, SIGBROILEREXIT);
err_pth:
	close(doorball_efd);
err_efd:
	return r;
}

static int doorball_io_exit(struct broiler *broiler, struct pci_device *pdev)
{
	u32 io_addr = pci_bar_address(pdev, 0);
	struct kvm_ioeventfd kvm_ioevent = {
		.fd	= doorball_efd,
		.addr	= io_addr + DOORBALL_REG,
		.len	= sizeof(u16),
		.flags	= KVM_IOEVENTFD_FLAG_DEASSIGN,
	};

	ioctl(broiler->vm_fd, KVM_IOEVENTFD, &kvm_ioevent);
	pthread_kill(doorball_thread, SIGBROILEREXIT);
	close(doorball_efd);

	return 0;
}

static int Broiler_pci_bar_active(struct broiler *broiler,
			struct pci_device *pdev, int bar, void *data)
{
	u32 bar_addr, bar_size;

	bar_addr = pci_bar_address(pdev, bar);
	bar_size = pci_bar_size(pdev, bar);

	return broiler_register_pio(broiler, bar_addr, bar_size,
				Broiler_pci_bar_callback, data);
}

static int Broiler_pci_bar_deactive(struct broiler *broiler,
			struct pci_device *pdev, int bar, void *data)
{
	u32 bar_addr;

	bar_addr = pci_bar_address(pdev, bar);

	return broiler_deregister_pio(broiler, bar_addr);
}

static int Broiler_pci_init(struct broiler *broiler)
{
	struct pci_device *pdev = &Broiler_pci_device;
	u16 io_addr;
	int r;

	/* IO-BAR */
	io_addr = pci_alloc_io_port_block(PCI_IO_SIZE);

	/* PCI Configuration Space */
	Broiler_pci_device = (struct pci_device) {
		.vendor_id	= 0x1001,
		.device_id	= 0x1991,
		.command	= PCI_COMMAND_IO | PCI_COMMAND_MEMORY,
		.header_type	= PCI_HEADER_TYPE_NORMAL,
		.revision_id	= 0,

		.bar[0]		= io_addr | PCI_BASE_ADDRESS_SPACE_IO,
		.bar_size[0]	= PCI_IO_SIZE,

		.status		= PCI_STATUS_CAP_LIST,
		.capabilities	= (void *)&pdev->msi - (void *)pdev,
	};

	r = pci_register_bar_regions(broiler, &Broiler_pci_device,
				Broiler_pci_bar_active,
				Broiler_pci_bar_deactive,
				(void *)&Broiler_pci_device);
	if (r < 0)
		return r;

	/* MSI */
	doorball_msi_init(broiler, pdev);

	/* Asynchronous IO */
	r = doorball_io_init(broiler, pdev);
	if (r)
		return r;

	r = device_register(&Broiler_device);
	if (r < 0)
		return r;

	return 0;
}
dev_init(Broiler_pci_init);

static int Broiler_pci_exit(struct broiler *broiler)
{
	device_unregister(&Broiler_device);
	doorball_io_exit(broiler, &Broiler_pci_device);
	
	return 0;
}
dev_exit(Broiler_pci_exit);
