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

	return 0;

err_bios_setup:
err_load_kernel:
	kvm_exit(broiler);
err_kvm:
	return ret;
}
