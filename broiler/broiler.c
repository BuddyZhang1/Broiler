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

	return 0;

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
