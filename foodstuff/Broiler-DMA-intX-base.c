/*
 * Broiler PCI DMA with INTX Interrupt
 *
 * (C) 2022.08.07 BuddyZhang1 <buddy.zhang@aliyun.com>
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
#include "broiler/memory.h"
#include "broiler/kvm.h"
#include <sys/eventfd.h>

/* DMA Register */
#define DMA_SRC_REG	0x00
#define DMA_DST_REG	0x04
#define DMA_DIRT_REG	0x08
#define DMA_LEN_REG	0x0C
#define DOORBALL_REG	0x10

#define DMA_BUFFER_LEN	4096
#define PCI_TO_DDR	0
#define DDR_TO_PCI	1

#define INTX_IRQ_LOW	0
#define INTX_IRQ_HIGH	1

static pthread_t doorball_thread;
static int intx_irq;
static int doorball_efd = 0;
/* Device Memory */
static char buffer[DMA_BUFFER_LEN] = "Weclome Broiler DMA, Advanced "
		"Programmable HypV Controller, CommandWord IOAPIC etc.";
static u64 DMA_src, DMA_dst, DMA_len, DMA_dirt;

static struct pci_device Broiler_pci_device;
static struct device Broiler_device = {
	.bus_type	= DEVICE_BUS_PCI,
	.data		= &Broiler_pci_device,
};

static inline u64 pci_to_hva(u64 dma_addr)
{
	return dma_addr + (unsigned long)buffer;
}

static void Broiler_pci_bar_callback(struct broiler_cpu *vcpu,
		u64 addr, u8 *data, u32 len, u8 is_write, void *ptr)
{
	struct pci_device *pdev = (struct pci_device *)ptr;
	u64 offset = addr - pci_bar_address(pdev, 0);

	if (is_write) { /* IO Write */
		switch (offset) {
		case DMA_SRC_REG:
			DMA_src = ioport_read32((void *)data);
			break;
		case DMA_DST_REG:
			DMA_dst = ioport_read32((void *)data);
			break;
		case DMA_LEN_REG:
			DMA_len	= ioport_read32((void *)data);
			break;
		case DMA_DIRT_REG:
			DMA_dirt = ioport_read32((void *)data);
			break;
		}
	} else { /* IO Read */
		switch (offset) {
		case DMA_SRC_REG:
			ioport_write32((void *)data, DMA_src);
			break;
		case DMA_DST_REG:
			ioport_write32((void *)data, DMA_dst);
			break;
		case DMA_LEN_REG:
			ioport_write32((void *)data, DMA_len);
			break;
		case DMA_DIRT_REG:
			ioport_write32((void *)data, DMA_dirt);
			break;
		}
	}
}

static int dma_ops(struct broiler *broiler)
{
	char *src_hva, *dst_hva;

	if (DMA_dirt == PCI_TO_DDR) { /* Memory Write TLP */
		src_hva = (char *)pci_to_hva(DMA_src);
		dst_hva = (char *)gpa_flat_to_hva(broiler, DMA_dst);
	} else if (DMA_dirt == DDR_TO_PCI) { /* Memory Read TLP */
		src_hva = (char *)gpa_flat_to_hva(broiler, DMA_src);
		dst_hva = (char *)pci_to_hva(DMA_dst);
	}
	return !!memcpy(dst_hva, src_hva, DMA_len);
}

static void *doorball_thdhands(void *dev)
{
	struct broiler *broiler = dev;
	u64 data;
	int r;

	while (1) {
		r = read(doorball_efd, &data, sizeof(u64));
		if (r < 0)
			continue;

		dma_ops(broiler);
		/* Emulate Asynchronous IO */
		sleep(5);

		/* Injuect INTx Interrupt */
		broiler_irq_line(broiler, intx_irq, INTX_IRQ_HIGH);
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
		.vendor_id	= 0x0309,
		.device_id	= 0x1989,
		.command	= PCI_COMMAND_IO | PCI_COMMAND_MEMORY,
		.header_type	= PCI_HEADER_TYPE_NORMAL,
		.revision_id	= 0,

		.bar[0]		= io_addr | PCI_BASE_ADDRESS_SPACE_IO,
		.bar_size[0]	= PCI_IO_SIZE,

		.status		= PCI_STATUS_CAP_LIST,
	};

	r = pci_register_bar_regions(broiler, &Broiler_pci_device,
				Broiler_pci_bar_active,
				Broiler_pci_bar_deactive,
				(void *)&Broiler_pci_device);
	if (r < 0)
		return r;

	/* Bind INTA/INTB/INTC/INTD */
	intx_irq = pci_assign_irq(pdev);
	broiler_irq_line(broiler, intx_irq, INTX_IRQ_LOW);

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
