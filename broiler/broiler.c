#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "broiler/broiler.h"
#include "broiler/kvm.h"

int broiler_base_init(struct broiler *broiler)
{
	int ret;

	/* KVM Env init */
	if (kvm_init(broiler) < 0) {
		printf("KVM init failed.\n");
		ret = -errno;
		goto err_kvm;
	}

	/* Load Firmware */
	if (broiler_load_kernel(broiler) < 0) {
		printf("Load kernel %s failed\n", broiler->kernel_name);
		ret = -errno;
		goto err_load_kernel;
	}

	/* Setup BIOS */
	if (broiler_setup_bios(broiler) < 0) {
		printf("Load BIOS failed.\n");
		ret = -errno;
		goto err_bios_setup;
	}

	/* IOEVENTFD */
	if (ioeventfd_init(broiler) < 0) {
		printf("IOeventfd failed.\n");
		ret = -errno;
		goto err_ioeventfd;
	}

	/* CPU */
	if (broiler_cpu_init(broiler) < 0) {
		printf("CPU init failed.\n");
		ret = -errno;
		goto err_cpu;
	}

	/* IRQ */
	if (broiler_irq_init(broiler) < 0) {
		printf("IRQ init failed.\n");
		ret = -errno;
		goto err_irq;
	}

	/* IOPORT */
	if (broiler_ioport_setup(broiler) < 0) {
		printf("IOport init failed.\n");
		ret = -errno;
		goto err_ioport;
	}

	/* PCI */
	if (broiler_pci_init(broiler) < 0) {
		printf("PCI Init failed.\n");
		ret = -errno;
		goto err_pci;
	}

	/* ROOTFS */
	if (broiler_disk_image_init(broiler) < 0) {
		printf("ROOTFS init failed.\n");
		ret = -errno;
		goto err_rootfs;
	}

	/* Keyboard and mouse  */
	if (broiler_keyboard_init(broiler) < 0) {
		printf("Keyboard init failed.\n");
		ret = -errno;
		goto err_keyboard;
	}

	/* Terminal and serial8250  */
	if (broiler_terminal_init(broiler) < 0) {
		printf("Terminal init failed.\n");
		ret = -errno;
		goto err_terminal;
	}

	/* RTC */
	if (broiler_rtc_init(broiler) < 0) {
		printf("RTC init failed.\n");
		ret = -errno;
		goto err_rtc;
	}

	/* virtio */
	if (broiler_virtio_init(broiler) < 0) {
		printf("VIRTIO init failed.\n");
		ret = -errno;
		goto err_virtio;
	}

	/* mptable */
	if (broiler_mptable_init(broiler) < 0) {
		printf("MPTABLE init failed.\n");
		ret = -errno;
		goto err_mptable;
	}

	/* threadpool */
	if (broiler_threadpool_init(broiler) < 0) {
		printf("Threadpool init failed.\n");
		ret = -errno;
		goto err_threadpool;
	}

	/* KVM Running */
	if (broiler_cpu_running(broiler) < 0) {
		printf("Broiler running failed.\n");
		ret = -errno;
		goto err_running;
	}

	return 0;

	broiler_cpu_exit(broiler);
err_running:
	broiler_threadpool_exit(broiler);
err_threadpool:
	broiler_mptable_exit(broiler);
err_mptable:
	broiler_virtio_exit(broiler);
err_virtio:
	broiler_rtc_exit(broiler);
err_rtc:
	broiler_terminal_exit(broiler);
err_terminal:
err_keyboard:
	broiler_disk_image_exit(broiler);
err_rootfs:
	broiler_pci_exit(broiler);
err_pci:
err_ioport:
err_irq:
err_cpu:
	ioeventfd_exit(broiler);
err_ioeventfd:
err_bios_setup:
err_load_kernel:
	kvm_exit(broiler);
err_kvm:
	return ret;
}
