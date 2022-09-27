/*
 * Broiler Asynchronous PIO
 *
 * (C) 2022.09.21 BuddyZhang1 <buddy.zhang@aliyun.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "broiler/broiler.h"
#include "broiler/ioport.h"
#include "broiler/utils.h"
#include "broiler/kvm.h"
#include "broiler/types.h"
#include "broiler/irq.h"
#include <sys/eventfd.h>
#include <assert.h>

#define BROILER_PIO_PORT	0x60A0
#define BROILER_PIO_LEN		0x10
#define DOORBALL_REG		0x00
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

		/* Emulate Asynchronous IO */
		sleep(2);

		/* Inject Interrupt */
		broiler_irq_line(broiler, irq, IRQ_HIGH);
	}
	pthread_exit(NULL);

	return NULL;
}

/* Emulate PIO */
static void Broiler_pio_callback(struct broiler_cpu *vcpu,
                u64 addr, u8 *data, u32 len, u8 is_write, void *ptr)
{
	assert(len == 4);
	assert(addr - BROILER_PIO_PORT == IRQ_NUM_REG);
	assert(!is_write);

	/* Read-Only */
	ioport_write32((void *)data, irq);
}

static int Broiler_pio_init(struct broiler *broiler)
{
	struct kvm_ioeventfd kvm_ioeventfd;
	int r;

	/* IRQ from Master or Slave IRQCHIP */
	irq = irq_alloc_from_irqchip();
	/* level trigger: set default level */
	broiler_irq_line(broiler, irq, IRQ_LOW);

	r = broiler_register_pio(broiler, BROILER_PIO_PORT,
			BROILER_PIO_LEN, Broiler_pio_callback, NULL);
	if (r < 0)
		goto err_pio;

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
		.addr	= BROILER_PIO_PORT,
		.len	= sizeof(u16),
		.fd	= irq_eventfd,
		.flags	= KVM_IOEVENTFD_FLAG_PIO,
	};

	syscall(600, 1);
	r = ioctl(broiler->vm_fd, KVM_IOEVENTFD, &kvm_ioeventfd);
	syscall(600, 0);
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
	broiler_deregister_pio(broiler, BROILER_PIO_PORT);
err_pio:
	return r;
}
dev_init(Broiler_pio_init);

static int Broiler_pio_exit(struct broiler *broiler)
{
	pthread_kill(irq_thread, SIGBROILEREXIT);
	close(irq_eventfd);
	broiler_deregister_pio(broiler, BROILER_PIO_PORT);

	return 0;
}
dev_exit(Broiler_pio_exit);
