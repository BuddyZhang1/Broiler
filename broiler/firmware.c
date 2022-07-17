// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <asm/bootparam.h>

#include "broiler/broiler.h"
#include "broiler/kvm.h"
#include "broiler/memory.h"
#include "broiler/utils.h"

static const char *BZIMAGE_MAGIC = "HdrS";

/*
 * See Documentation/x86/boot.txt for details no bzImage
 * on-disk and memory layout.
 */
int broiler_load_kernel(struct broiler *broiler)
{
	struct boot_params boot, *kern_boot;
	size_t cmdline_size;
	size_t file_size;
	int kernel_fd;
	void *p;
	int ret;

	/* Open kernel bzImage */
	kernel_fd = open(broiler->kernel_name, O_RDONLY);
	if (kernel_fd < 0) {
		printf("Unable to open kernel %s\n", broiler->kernel_name);
		ret = -errno;
		goto err_open_kernel;
	}

	/* Read bzImage boot params header */
	if (read(kernel_fd, &boot, sizeof(boot)) != sizeof(boot)) {
		printf("Unable to read bzImage boot_params.\n");
		ret = -errno;
		goto err_read_boot_params;
	}

	/* Check BZIMAGE_MAGIC */
	if (memcmp(&boot.hdr.header, BZIMAGE_MAGIC, strlen(BZIMAGE_MAGIC))) {
		printf("Unexpect Kernel MAGIC.\n");
		ret = -errno;
		goto err_kernel_magic;
	}

	if (boot.hdr.version < BOOT_PROTOCOL_REQUIRED) {
		printf("Kernel too old.\n");
		ret = -errno;
		goto err_version;
	}

	if (lseek(kernel_fd, 0, SEEK_SET) < 0) {
		printf("SEEK 0 failed.\n");
		ret = -errno;
		goto err_lseek;
	}

	/* Kernel bootloader: setup */
	file_size = (boot.hdr.setup_sects + 1) << 9;
	p = gpa_real_to_hva(broiler, BOOT_LOADER_SELECTOR, BOOT_LOADER_IP);
	if (read_in_full(kernel_fd, p, file_size) != file_size) {
		printf("Kernel Setup read failed.\n");
		ret = -errno;
		goto err_setup;
	}

	/* Load actual kernel image (vmlinux.bin) to BZ_KERNEL_START */
	p = gpa_flat_to_hva(broiler, BZ_KERNEL_START);
	if (read_file(kernel_fd, p, broiler->ram_size - BZ_KERNEL_START) < 0) {
		printf("Kernel vmlinux read failed.\n");
		ret = -errno;
		goto err_load_kernel;
	}

	/* Load CMDLINE */
	p = gpa_flat_to_hva(broiler, BOOT_CMDLINE_OFFSET);
	cmdline_size = strlen(broiler->cmdline) + 1;
	if (cmdline_size > boot.hdr.cmdline_size)
		cmdline_size = boot.hdr.cmdline_size;
	memset(p, 0, boot.hdr.cmdline_size);
	memcpy(p, broiler->cmdline, cmdline_size - 1);

	/* kernel boot params */
	kern_boot = gpa_real_to_hva(broiler, BOOT_LOADER_SELECTOR, 0x00);
	kern_boot->hdr.cmd_line_ptr	= BOOT_CMDLINE_OFFSET;
	kern_boot->hdr.type_of_loader	= 0xff;
	kern_boot->hdr.heap_end_ptr	= 0xFE00;
	kern_boot->hdr.loadflags	|= CAN_USE_HEAP;
	kern_boot->hdr.vid_mode		= 0;

	/*
	 * The real-mode setup code starts at offset 0x200 of a bzImage.
	 * See Documentation/x86/boot.txt for details.
	 */
	broiler->boot_selector = BOOT_LOADER_SELECTOR;
	broiler->boot_ip = BOOT_LOADER_IP + 0x200;
	broiler->boot_sp = BOOT_LOADER_SP;

	close(kernel_fd);
	return 0;

err_load_kernel:
err_setup:
err_lseek:
err_version:
err_kernel_magic:
err_read_boot_params:
	close(kernel_fd);
err_open_kernel:
	return ret;
}
