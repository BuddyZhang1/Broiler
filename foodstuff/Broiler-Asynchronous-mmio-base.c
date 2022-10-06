/*
 * Broiler Asynchronous MMIO
 *
 * (C) 2022.10.01 BuddyZhang1 <buddy.zhang@aliyun.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "broiler/broiler.h"
#include "broiler/ioport.h"
#include "broiler/utils.h"
#include "broiler/types.h"
#include "broiler/irq.h"
#include <sys/eventfd.h>
#include <assert.h>

#define BROILER_MMIO_BASE	0xD0000040
#define BROILER_MMIO_LEN	0x10
#define BOORBALL_REG		0x00
#define IRQ_NUM_REG		0x04
#define IRQ_LOW			0
#define IRQ_HIGH		1

/* interrupt irq */
static pthread_t irq_thread;
static int irq_eventfd;
static int irq;

static void *irq_threads(void *dev)
{
	struct broiler *broiler = dev;
	u64 data;
	int r;

	while (1) { 
		r = read(irq_eventfd, &data, sizeof(u64));
		if (r < 0)
			continue;

		/* Emulate Asynchronous MMIO */
		sleep(2);

		/* Inject Interrupt */
		broiler_irq_line(broiler, irq, IRQ_HIGH);
	}
	pthread_exit(NULL);

	return NULL;
}

/* Emulate MMIO */
static void Broiler_mmio_callback(struct broiler_cpu *vcpu,
		u64 addr, u8 *data, u32 len, u8 is_write, void *ptr)
{
	assert(len == 4);
	assert(addr - BROILER_MMIO_BASE == IRQ_NUM_REG);
	assert(!is_write);

	/* Read-Only */
	ioport_write32((void *)data, irq);
}

static int Broiler_mmio_init(struct broiler *broiler)
{
	struct kvm_ioeventfd kvm_ioeventfd;
	int r;

	/* IRQ from Master or Slave IRQCHIP */
	irq = irq_alloc_from_irqchip();
	/* level trigger: set default level */
	broiler_irq_line(broiler, irq, IRQ_LOW);

	/* Register MMIO Region */
	r = broiler_ioport_register(broiler, BROILER_MMIO_BASE + IRQ_NUM_REG,
			BROILER_MMIO_LEN - IRQ_NUM_REG, 
			Broiler_mmio_callback, NULL, DEVICE_BUS_MMIO);
	if (r < 0)
		goto err_mmio;

	/* Asynchronous IO */
	irq_eventfd = eventfd(0, 0);
	if (irq_eventfd < 0) {
		r = -errno;
		goto err_eventfd;
	}

	if (pthread_create(&irq_thread, NULL, irq_threads, broiler)) {
		r = -errno;
		goto err_thread;
	}

	/* KVM Asynchronous IO */
	kvm_ioeventfd = (struct kvm_ioeventfd) {
		.addr   = BROILER_MMIO_BASE + BOORBALL_REG,
		.len    = sizeof(u32),
		.fd     = irq_eventfd,
		.flags  = 0, /* MMIO Must 0 */
	};

	r = ioctl(broiler->vm_fd, KVM_IOEVENTFD, &kvm_ioeventfd);
	if (r) {
		r = -errno;
		goto err_ioctl;
	}

	return 0;

err_ioctl:
	pthread_kill(irq_thread, SIGBROILEREXIT);
err_thread:
	close(irq_eventfd);
err_eventfd:
	broiler_ioport_deregister(broiler, BROILER_MMIO_BASE,
					DEVICE_BUS_MMIO);
err_mmio:
	return r;
}
dev_init(Broiler_mmio_init);

static int Broiler_mmio_exit(struct broiler *broiler)
{
	pthread_kill(irq_thread, SIGBROILEREXIT);
	close(irq_eventfd);
	broiler_ioport_deregister(broiler, BROILER_MMIO_BASE,
					DEVICE_BUS_MMIO);

	return 0;
}
dev_exit(Broiler_mmio_exit);
