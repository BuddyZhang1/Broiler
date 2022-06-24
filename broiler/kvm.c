#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/kvm.h>
#include "broiler/broiler.h"
#include "broiler/virtio.h"

bool kvm_support_extension(struct broiler *broiler, unsigned int extension)
{
	if (ioctl(broiler->kvm_fd, KVM_CHECK_EXTENSION, extension) < 0)
		return false;
	return true;
}

int __attribute__((weak)) kvm_cpu_get_endianness(struct kvm_cpu *vcpu)
{
	return VIRTIO_ENDIAN_HOST;
}

void broiler_reboot(struct broiler *broiler)
{
	/* Check if the guest is running */
	printf("Broiler Reboot!\n");
}

int kvm_init(struct broiler *broiler)
{
	struct kvm_userspace_memory_region mem;
	int ret;

	/* Open KVM */
	broiler->kvm_fd = open("/dev/kvm", O_RDWR);
	if (broiler->kvm_fd < 0) {
		printf("/dev/kvm doesn't exist\n");
		ret = -errno;
		goto err_open_kvm;
	}

	/* Check Version */
	ret = ioctl(broiler->kvm_fd, KVM_GET_API_VERSION, 0);
	if (ret != KVM_API_VERSION) {
		printf("KVM_API_VERSION ioctl.\n");
		ret = -errno;
		goto err_kvm_version;
	}

	/* Create KVM VM */
	broiler->vm_fd = ioctl(broiler->kvm_fd, KVM_CREATE_VM, 0);
	if (broiler->vm_fd < 0) {
		printf("KVM_CREATE_VM failed\n");
		ret = -errno;
		goto err_create_vm;
	}
	
	/* Set TSS */
	ret = ioctl(broiler->vm_fd, KVM_SET_TSS_ADDR, 0xFFFBD000);
	if (ret < 0) {
		printf("KVM_SET_TSS_ADDR error.\n");
		ret = -errno;
		goto err_set_tss;
	}

	/* Alloc HVA */
	broiler->hva_start = mmap(NULL, broiler->ram_size, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
					-1, 0);
	if (broiler->hva_start == MAP_FAILED) {
		printf("HVA alloc failed.\n");
		ret = -errno;
		goto err_hva_alloc;
	}

	/* KVM create memory slot */
	mem = (struct kvm_userspace_memory_region) {
		.slot			= 0,
		.flags			= 0,
		.guest_phys_addr	= 0,
		.memory_size		= broiler->ram_size,
		.userspace_addr		= (unsigned long)broiler->hva_start,
	};
	ret = ioctl(broiler->vm_fd, KVM_SET_USER_MEMORY_REGION, &mem);
	if (ret < 0) {
		ret = -errno;
		goto err_memslot;
	}

	/* PCI Region */
	broiler->pci_base = mem.guest_phys_addr + mem.memory_size;

	/* IRQ */
	ret = ioctl(broiler->vm_fd, KVM_CREATE_IRQCHIP);
	if (ret < 0) {
		printf("KVM_CREATE_IRQCHIP\n");
		ret = -errno;
		goto err_create_irqchip;
	}

	return 0;

err_create_irqchip:
err_memslot:
	munmap(broiler->hva_start, broiler->ram_size);
err_hva_alloc:
err_set_tss:
err_create_vm:
err_kvm_version:
	close(broiler->kvm_fd);
err_open_kvm:
	return ret;
}

void kvm_exit(struct broiler *broiler)
{

}
