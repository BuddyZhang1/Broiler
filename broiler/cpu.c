#include <sys/mman.h>
#include <sys/eventfd.h>

#include "broiler/broiler.h"
#include "broiler/apicdef.h"

static int task_eventfd;

static int broiler_cpu_set_lint(struct broiler_cpu *vcpu)
{
	struct local_apic lapic;

	if (ioctl(vcpu->vcpu_fd, KVM_GET_LAPIC, &lapic))
		return -1;

	lapic.lvt_lint0.delivery_mode = APIC_MODE_EXTINT;
	lapic.lvt_lint1.delivery_mode = APIC_MODE_NMI;

	return ioctl(vcpu->vcpu_fd, KVM_SET_LAPIC, &lapic);
}

static struct broiler_cpu *
broiler_cpu_init_one(struct broiler *broiler, unsigned long cpu_id)
{
	int coalesced_offset;
	struct broiler_cpu *vcpu;
	int mmap_size;
	int ret;

	vcpu = calloc(1, sizeof(*vcpu));
	if (!vcpu)
		return NULL;
	vcpu->broiler = broiler;

	vcpu->cpu_id = cpu_id;

	vcpu->vcpu_fd = ioctl(broiler->vm_fd, KVM_CREATE_VCPU, cpu_id);
	if (vcpu->vcpu_fd < 0) {
		printf("KVM_CREATE_VCPU failed.\n");
		return NULL;
	}

	mmap_size = ioctl(broiler->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
	if (mmap_size < 0) {
		printf("KVM_GET_VCPU_MMAP_SIZE ioctl\n");
		return NULL;
	}

	vcpu->kvm_run = mmap(NULL, mmap_size,
				PROT_READ | PROT_WRITE,
				MAP_SHARED,
				vcpu->vcpu_fd,
				0);
	if (vcpu->kvm_run == MAP_FAILED) {
		printf("Unable to mmap vcpu fd\n");
		return NULL;
	}

	coalesced_offset = ioctl(broiler->kvm_fd, KVM_CHECK_EXTENSION,
						KVM_CAP_COALESCED_MMIO);
	if (coalesced_offset)
		vcpu->ring = (void *)vcpu->kvm_run + 
					(coalesced_offset * PAGE_SIZE);
	
	if (broiler_cpu_set_lint(vcpu)) {
		printf("KVM_SET_LAPIC failed.\n");
		return NULL;
	}

	vcpu->is_running = true;

	return vcpu;
}

int broiler_cpu_init(struct broiler *broiler)
{
	int i;

	task_eventfd = eventfd(0, 0);
	if (task_eventfd < 0) {
		printf("Couldn't create task_eventfd\n");
		return task_eventfd;
	}

	/* Alooc one pointer too many, so array ends up 0-terminated */
	broiler->cpus = calloc(broiler->nr_cpu + 1, sizeof(void *));
	if (!broiler->cpus) {
		printf("Couldn't allocate array for CPUs\n");
		return -ENOMEM;
	}

	for (i = 0; i < broiler->nr_cpu; i++) {
		broiler->cpus[i] = broiler_cpu_init_one(broiler, i);
		if (!broiler->cpus[i]) {
			printf("Unable to initialize VCPU.\n");
			goto err_vcpu;
		}
	}

	return 0;

err_vcpu:
	for (i = 0; i < broiler->nr_cpu; i++)
		free(broiler->cpus[i]);
	return -ENOMEM;
}
