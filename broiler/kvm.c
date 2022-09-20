// SPDX-License-Identifier: GPL-2.0-only
#include "broiler/broiler.h"
#include "broiler/virtio.h"
#include "broiler/kvm.h"
#include "broiler/utils.h"
#include "broiler/err.h"
#include "broiler/ioport.h"
#include "broiler/memory.h"
#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <signal.h>
#include <limits.h>

#define DEFINE_BROILER_EXIT_REASON(reason) [reason] = #reason

const char *broiler_exit_reasons[] = {
	DEFINE_BROILER_EXIT_REASON(KVM_EXIT_UNKNOWN),
	DEFINE_BROILER_EXIT_REASON(KVM_EXIT_EXCEPTION),
	DEFINE_BROILER_EXIT_REASON(KVM_EXIT_IO),
	DEFINE_BROILER_EXIT_REASON(KVM_EXIT_HYPERCALL),
	DEFINE_BROILER_EXIT_REASON(KVM_EXIT_DEBUG),
	DEFINE_BROILER_EXIT_REASON(KVM_EXIT_HLT),
	DEFINE_BROILER_EXIT_REASON(KVM_EXIT_MMIO),
	DEFINE_BROILER_EXIT_REASON(KVM_EXIT_IRQ_WINDOW_OPEN),
	DEFINE_BROILER_EXIT_REASON(KVM_EXIT_SHUTDOWN),
	DEFINE_BROILER_EXIT_REASON(KVM_EXIT_FAIL_ENTRY),
	DEFINE_BROILER_EXIT_REASON(KVM_EXIT_INTR),
	DEFINE_BROILER_EXIT_REASON(KVM_EXIT_SET_TPR),
	DEFINE_BROILER_EXIT_REASON(KVM_EXIT_TPR_ACCESS),
	DEFINE_BROILER_EXIT_REASON(KVM_EXIT_S390_SIEIC),
	DEFINE_BROILER_EXIT_REASON(KVM_EXIT_S390_RESET),
	DEFINE_BROILER_EXIT_REASON(KVM_EXIT_DCR),
	DEFINE_BROILER_EXIT_REASON(KVM_EXIT_NMI),
	DEFINE_BROILER_EXIT_REASON(KVM_EXIT_INTERNAL_ERROR),
};

struct kvm_ext kvm_req_ext[] = {
	{ DEFINE_KVM_EXT(KVM_CAP_COALESCED_MMIO) },
	{ DEFINE_KVM_EXT(KVM_CAP_SET_TSS_ADDR) },
	{ DEFINE_KVM_EXT(KVM_CAP_PIT2) },
	{ DEFINE_KVM_EXT(KVM_CAP_USER_MEMORY) },
	{ DEFINE_KVM_EXT(KVM_CAP_IRQ_ROUTING) },
	{ DEFINE_KVM_EXT(KVM_CAP_IRQCHIP) },
	{ DEFINE_KVM_EXT(KVM_CAP_HLT) },
	{ DEFINE_KVM_EXT(KVM_CAP_IRQ_INJECT_STATUS) },
	{ DEFINE_KVM_EXT(KVM_CAP_EXT_CPUID) },
	{ 0, 0 }
};

static int pause_event;
static DEFINE_MUTEX(pause_lock);
__thread struct broiler_cpu *current_broiler_cpu;

bool kvm_support_extension(struct broiler *broiler, unsigned int extension)
{
	if (ioctl(broiler->kvm_fd, KVM_CHECK_EXTENSION, extension) <= 0)
		return false;
	return true;
}

static int kvm_check_extensions(struct broiler *broiler)
{
	int i;

	for (i = 0; ; i++) {
		if (!kvm_req_ext[i].name)
			break;
		if (!kvm_support_extension(broiler, kvm_req_ext[i].code)) {
			printf("Unsupport KVM extension: %s\n",
						kvm_req_ext[i].name);
			return -i;
		}
	}
	return 0;
}

static inline bool is_in_protected_mode(struct broiler_cpu *vcpu)
{
	return vcpu->sregs.cr0 & 0x01;
}

static inline u64 ip_to_flat(struct broiler_cpu *vcpu, u64 ip)
{
	u64 cs;

	/*
	 * NOTE! We should take code segment base address into account here.
	 * Luckily it's usually zero because Linux uses flat memory model.
	 */
	if (is_in_protected_mode(vcpu))
		return ip;

	cs = vcpu->sregs.cs.selector;

	return ip + (cs << 4);
}

int __attribute__((weak)) broiler_cpu_get_endianness(struct broiler_cpu *vcpu)
{
	return VIRTIO_ENDIAN_HOST;
}

void broiler_reboot(struct broiler *broiler)
{
	/* Check if the guest is running */
	if (!broiler->cpus[0] || broiler->cpus[0]->thread == 0)
		return;

	pthread_kill(broiler->cpus[0]->thread, SIGBROILEREXIT);
}

static bool broiler_cpu_handle_exit(struct broiler_cpu *vcpu)
{
	return false;
}

static void broiler_notify_paused(void)
{
	u64 p = 1;

	if (write(pause_event, &p, sizeof(p)))
		die("Failed notifying of paused VCPU.");

	mutex_lock(&pause_lock);
	current_broiler_cpu->paused = 0;
	mutex_unlock(&pause_lock);
}

static void broiler_cpu_signal_handler(int signum)
{
	if (signum == SIG_BROILER_EXIT) {
		if (current_broiler_cpu && current_broiler_cpu->is_running)
			current_broiler_cpu->is_running = false;
	} else if (signum == SIG_BROILER_PAUSE) {
		if (current_broiler_cpu->paused)
			die("Pause signaled for already paused CPU\n");

		/* pause_lock is held by broiler_pause() */
		current_broiler_cpu->paused = 1;

		/*
		 * This is a blocking function and uses locks. It is safe
		 * to call it for this signal as a second pause event should
		 * not be send to this thread until it acquires and releases
		 * the pause_lock.
		 */
		broiler_notify_paused();
	}

	/* For SIG_BROILER_TASK cpu->task is already set */
}

static void filter_cpuid(struct kvm_cpuid2 *broiler_cpuid, int cpu_id)
{
	unsigned int i;

	/*
	 * Filer CPUID functions that are not supported by the hypervisor
	 */
	for (i = 0; i < broiler_cpuid->nent; i++) {
		struct kvm_cpuid_entry2 *entry = &broiler_cpuid->entries[i];

		switch (entry->function) {
		case 0:
			/* BiscuitOS */
			entry->ebx = 0x63736942; /* Bisc */
			entry->edx = 0x4F746975; /* uitO */
			entry->ecx = 0x53; /* S */ 
			break;
		case 0x80000002: /* Broiler@16th Gen Intel(R) @ 5.50GHz*/
			entry->eax = 0x696F7242; /* Broi */
			entry->ebx = 0x4072656C; /* ler@ */
			entry->ecx = 0x68743631; /* 16th */
			entry->edx = 0x65705320; /*  Spe */
			break;
		case 0x80000003:
			entry->eax = 0x746E4920; /*  Int */
			entry->ebx = 0x52286C65; /* el(R */
			entry->ecx = 0x20402029; /* ) @ */
			entry->edx = 0x30352E35; /* 5.50 */
			break;
		case 0x80000004:
			entry->eax = 0x7A4847; /* GHz */
			break;
		case 1:
			entry->ebx & ~(0xff << 24);
			entry->ebx |= cpu_id << 24;
			/* Set X86_FEATURE_HYPERVISOR */
			if (entry->index == 0)
				entry->ecx |= (1 << 31);
			break;
		case 6:
			/* Clear X86_FEATURE_EPB */
			entry->ecx = entry->ecx & ~(1 << 3);
			break;
		case 10: { /* Architectural Performance Monitoring */
			union cpuid10_eax {
				struct {
					unsigned int version_id		:8;
					unsigned int num_counters	:8;
					unsigned int bit_width		:8;
					unsigned int mask_length	:8;
				} split;
				unsigned int full;
			} eax;

			/*
			 * If the host has perf system running,
			 * but no architectural events available
			 * through kvm pmu -- disable perf support,
			 * thus guest won't event try to access msr
			 * registers.
			 */
			if (entry->eax) {
				eax.full = entry->eax;
				if (eax.split.version_id != 2 ||
					!eax.split.num_counters)
					entry->eax = 0;
			}
			break;
		}
		default:
			/* Keep the CPUID function */
			break;
		}
	}
}

void broiler_continue(struct broiler *broiler)
{
	mutex_unlock(&pause_lock);
}

void broiler_pause(struct broiler *broiler)
{
	int i, paused_vcpus = 0;

	mutex_lock(&pause_lock);

	/* Check if the guest is running */
	if (!broiler->cpus || !broiler->cpus[0] ||
					broiler->cpus[0]->thread == 0)
		return;

	pause_event = eventfd(0, 0);
	if (pause_event < 0)
		die("Failed creating pause notification event");
	for (i = 0; i < broiler->nr_cpu; i++) {
		if (broiler->cpus[i]->is_running &&
					broiler->cpus[i]->paused == 0)
			pthread_kill(broiler->cpus[i]->thread,
							SIGBROILERPAUSE);
		else
			paused_vcpus++;
	}

	while (paused_vcpus < broiler->nr_cpu) {
		u64 cur_read;

		if (read(pause_event, &cur_read, sizeof(cur_read)) < 0)
			die("Failed reading pause event");
		paused_vcpus += cur_read;
	}
	close(pause_event);
}

static void print_dtable(const char *name, struct kvm_dtable *dtable)
{               
	dprintf(STDOUT_FILENO, " %s                 %016llx  %08hx\n",
		name, (u64) dtable->base, (u16) dtable->limit);
}

static void print_segment(const char *name, struct kvm_segment *seg)
{
	dprintf(STDOUT_FILENO, " %s       %04hx      %016llx  %08x  %02hhx"
			  "    %x %x   %x  %x %x %x %x\n",
			name, (u16) seg->selector, (u64) seg->base,
			(u32) seg->limit, (u8) seg->type, seg->present,
			seg->dpl, seg->db, seg->s, seg->l, seg->g, seg->avl);
}

static void broiler_cpu_dump_registers(struct broiler_cpu *vcpu)
{
	unsigned long cr0, cr2, cr3;
	unsigned long cr4, cr8;
	unsigned long rax, rbx, rcx;
	unsigned long rdx, rsi, rdi;
	unsigned long rbp,  r8,  r9;
	unsigned long r10, r11, r12;
	unsigned long r13, r14, r15;
	unsigned long rip, rsp;
	struct kvm_sregs sregs;
	unsigned long rflags;
	struct kvm_regs regs;
	int debug_fd = STDOUT_FILENO;
	int i;

	if (ioctl(vcpu->vcpu_fd, KVM_GET_REGS, &regs) < 0)
		die("KVM_GET_REGS failed");

	rflags = regs.rflags;

	rip = regs.rip; rsp = regs.rsp;
	rax = regs.rax; rbx = regs.rbx; rcx = regs.rcx;
	rdx = regs.rdx; rsi = regs.rsi; rdi = regs.rdi;
	rbp = regs.rbp; r8  = regs.r8;  r9  = regs.r9;
	r10 = regs.r10; r11 = regs.r11; r12 = regs.r12;
	r13 = regs.r13; r14 = regs.r14; r15 = regs.r15;

	dprintf(debug_fd, "\n Registers:\n");
	dprintf(debug_fd,   " ----------\n");
	dprintf(debug_fd, " rip: %016lx   rsp: %016lx flags: %016lx\n",
							rip, rsp, rflags);
	dprintf(debug_fd, " rax: %016lx   rbx: %016lx   rcx: %016lx\n",
							rax, rbx, rcx);
	dprintf(debug_fd, " rdx: %016lx   rsi: %016lx   rdi: %016lx\n",
							rdx, rsi, rdi);
	dprintf(debug_fd, " rbp: %016lx    r8: %016lx    r9: %016lx\n",
							rbp, r8,  r9);
	dprintf(debug_fd, " r10: %016lx   r11: %016lx   r12: %016lx\n",
							r10, r11, r12);
	dprintf(debug_fd, " r13: %016lx   r14: %016lx   r15: %016lx\n",
							r13, r14, r15);

	if (ioctl(vcpu->vcpu_fd, KVM_GET_SREGS, &sregs) < 0)
		die("KVM_GET_REGS failed");

	cr0 = sregs.cr0; cr2 = sregs.cr2; cr3 = sregs.cr3;
	cr4 = sregs.cr4; cr8 = sregs.cr8;

	dprintf(debug_fd, " cr0: %016lx   cr2: %016lx   cr3: %016lx\n", cr0, cr2, cr3);
	dprintf(debug_fd, " cr4: %016lx   cr8: %016lx\n", cr4, cr8);
	dprintf(debug_fd, "\n Segment registers:\n");
	dprintf(debug_fd,   " ------------------\n");
	dprintf(debug_fd, " register  selector  base              limit     type  p dpl db s l g avl\n");
	print_segment("cs ", &sregs.cs);
	print_segment("ss ", &sregs.ss);
	print_segment("ds ", &sregs.ds);
	print_segment("es ", &sregs.es);
	print_segment("fs ", &sregs.fs);
	print_segment("gs ", &sregs.gs);
	print_segment("tr ", &sregs.tr);
	print_segment("ldt", &sregs.ldt);
	print_dtable("gdt", &sregs.gdt);
	print_dtable("idt", &sregs.idt);

	dprintf(debug_fd, "\n APIC:\n");
	dprintf(debug_fd,   " -----\n");
	dprintf(debug_fd, " efer: %016llx  apic base: %016llx  nmi: %s\n",
			(u64) sregs.efer, (u64) sregs.apic_base,
			(vcpu->broiler->nmi_disabled ? "disabled" : "enabled"));

	dprintf(debug_fd, "\n Interrupt bitmap:\n");
	dprintf(debug_fd,   " -----------------\n");
	for (i = 0; i < (KVM_NR_INTERRUPTS + 63) / 64; i++)
		dprintf(debug_fd, " %016llx", (u64) sregs.interrupt_bitmap[i]);
	dprintf(debug_fd, "\n");
}

static void broiler_dump_memory(struct broiler *broiler,
		unsigned long addr, unsigned long size, int debug_fd)
{
	unsigned char *p;
	unsigned long n;

	size &= ~7; /* mod 8 */
	if (!size)      
		return;    

	p = gpa_flat_to_hva(broiler, addr);

	for (n = 0; n < size; n += 8) {
		if (!hva_ptr_in_ram(broiler, p + n)) {
			dprintf(debug_fd, " 0x%08lx: <unknown>\n", addr + n);
			continue;
		}
		dprintf(debug_fd, " 0x%08lx: %02x %02x %02x %02x  "
				  "%02x %02x %02x %02x\n",
			addr + n, p[n + 0], p[n + 1], p[n + 2], p[n + 3],
			p[n + 4], p[n + 5], p[n + 6], p[n + 7]);
	}
}

static inline char *
symbol_lookup(struct broiler *kvm, unsigned long addr, char *sym, size_t size)
{               
	char *s = strncpy(sym, SYMBOL_DEFAULT_UNKNOWN, size);

	sym[size - 1] = '\0';
	return s;
}

static void broiler_cpu_dump_code(struct broiler_cpu *vcpu)
{
	unsigned int code_bytes = 64;
	unsigned int code_prologue = 43;
	unsigned int code_len = code_bytes;
	char sym[MAX_SYM_LEN] = SYMBOL_DEFAULT_UNKNOWN, *psym;
	int debug_fd = STDOUT_FILENO;
	unsigned char c;
	unsigned int i;
	u8 *ip;

	if (ioctl(vcpu->vcpu_fd, KVM_GET_REGS, &vcpu->regs) < 0)
		die("KVM_GET_REGS failed");

	if (ioctl(vcpu->vcpu_fd, KVM_GET_SREGS, &vcpu->sregs) < 0)
		die("KVM_GET_SREGS failed");

	ip = gpa_flat_to_hva(vcpu->broiler,
			ip_to_flat(vcpu, vcpu->regs.rip) - code_prologue);

	dprintf(debug_fd, "\n Code:\n");
	dprintf(debug_fd,   " -----\n");

	psym = symbol_lookup(vcpu->broiler, vcpu->regs.rip, sym, MAX_SYM_LEN);
	if (IS_ERR(psym))
		dprintf(debug_fd,
			"Warning: symbol_lookup() failed to find symbol "
			"with error: %ld\n", PTR_ERR(psym));

	dprintf(debug_fd, " rip: [<%016lx>] %s\n\n",
				(unsigned long) vcpu->regs.rip, sym);

	for (i = 0; i < code_len; i++, ip++) {
		if (!hva_ptr_in_ram(vcpu->broiler, ip))
			break;

		c = *ip;

		if (ip == gpa_flat_to_hva(vcpu->broiler,
					ip_to_flat(vcpu, vcpu->regs.rip)))
			dprintf(debug_fd, " <%02x>", c);
		else
			dprintf(debug_fd, " %02x", c);
	}

	dprintf(debug_fd, "\n");

	dprintf(debug_fd, "\n Stack:\n");
	dprintf(debug_fd,   " ------\n");
	dprintf(debug_fd, " rsp: [<%016lx>] \n",
				(unsigned long) vcpu->regs.rsp);
	broiler_dump_memory(vcpu->broiler, vcpu->regs.rsp, 32, debug_fd);
}

static void broiler_cpu_setup_cpuid(struct broiler_cpu *vcpu)
{
	struct kvm_cpuid2 *broiler_cpuid;

	broiler_cpuid = calloc(1, sizeof(*broiler_cpuid) +
		MAX_KVM_CPUID_ENTRIES * sizeof(*broiler_cpuid->entries));
	broiler_cpuid->nent = MAX_KVM_CPUID_ENTRIES;

	if (ioctl(vcpu->broiler->kvm_fd, 
				KVM_GET_SUPPORTED_CPUID, broiler_cpuid) < 0)
		die_perror("KVM_GET_SUPPORT_CPUID failed");

	filter_cpuid(broiler_cpuid, vcpu->cpu_id);

	if (ioctl(vcpu->vcpu_fd, KVM_SET_CPUID2, broiler_cpuid) < 0)
		die_perror("KVM_SET_CPUID2 failed");

	free(broiler_cpuid);
}

static void broiler_cpu_handle_coalesced_mmio(struct broiler_cpu *cpu)
{
	if (cpu->ring) {
		while (cpu->ring->first != cpu->ring->last) {
			struct kvm_coalesced_mmio *m;

			m = &cpu->ring->coalesced_mmio[cpu->ring->first];
			broiler_cpu_emulate_mmio(cpu,
						 m->phys_addr,
						 m->data,
						 m->len,
						 1);
			cpu->ring->first = (cpu->ring->first + 1) %
						KVM_COALESCED_MMIO_MAX;
		}
	}
}

static inline u32 selector_to_base(u16 selector)
{
	/*
	 * KVM on Intel requires 'base' to be 'selector * 16' in real mode.
	 */
	return (u32)selector << 4;
}

static void broiler_cpu_setup_sregs(struct broiler_cpu *vcpu)
{
	struct broiler *broiler = vcpu->broiler;

	if (ioctl(vcpu->vcpu_fd, KVM_GET_SREGS, &vcpu->sregs) < 0)
		die_perror("KVM_GET_SREGS failed");

	/* CS Segment */
	vcpu->sregs.cs.selector	= broiler->boot_selector;
	vcpu->sregs.cs.base	= selector_to_base(broiler->boot_selector);
	/* SS Segment */
	vcpu->sregs.ss.selector	= broiler->boot_selector;
	vcpu->sregs.ss.base	= selector_to_base(broiler->boot_selector);
	/* DS Segment */
	vcpu->sregs.ds.selector	= broiler->boot_selector;
	vcpu->sregs.ds.base	= selector_to_base(broiler->boot_selector);
	/* ES Segment */
	vcpu->sregs.es.selector	= broiler->boot_selector;
	vcpu->sregs.es.base	= selector_to_base(broiler->boot_selector);
	/* FS Segment */
	vcpu->sregs.fs.selector	= broiler->boot_selector;
	vcpu->sregs.fs.base	= selector_to_base(broiler->boot_selector);
	/* GS Segment */
	vcpu->sregs.gs.selector = broiler->boot_selector;
	vcpu->sregs.gs.base	= selector_to_base(broiler->boot_selector);

	if (ioctl(vcpu->vcpu_fd, KVM_SET_SREGS, &vcpu->sregs) < 0)
		die_perror("KVM_SET_SREGS failed");
}

static void broiler_cpu_setup_regs(struct broiler_cpu *vcpu)
{
	vcpu->regs = (struct kvm_regs) {
		/* We start the guest in 16-bit real mode */
		.rflags = 0x0000000000000002ULL,
		.rip	= vcpu->broiler->boot_ip,
		.rsp	= vcpu->broiler->boot_sp,
		.rbp	= vcpu->broiler->boot_sp,
	};

	if (vcpu->regs.rip > USHRT_MAX)
		die("IP %0xllx is too high for real mode", (u64)vcpu->regs.rip);

	if (ioctl(vcpu->vcpu_fd, KVM_SET_REGS, &vcpu->regs) < 0)
		die_perror("KVM_SET_REGS failed");
}

static void broiler_cpu_setup_msrs(struct broiler_cpu *vcpu)
{
	unsigned long ndx = 0;

	vcpu->msrs = calloc(1, sizeof(struct kvm_msrs) +
			      (sizeof(struct kvm_msr_entry) * 100));
	if (!vcpu->msrs)
		die("out of memory on msrs");

	vcpu->msrs->entries[ndx++] =
			BROILER_MSR_ENTRY(MSR_IA32_SYSENTER_CS, 0x00);
	vcpu->msrs->entries[ndx++] =
			BROILER_MSR_ENTRY(MSR_IA32_SYSENTER_ESP, 0x00);
	vcpu->msrs->entries[ndx++] =
			BROILER_MSR_ENTRY(MSR_IA32_SYSENTER_EIP, 0x00);
	vcpu->msrs->entries[ndx++] =
			BROILER_MSR_ENTRY(MSR_STAR, 0x00);
	vcpu->msrs->entries[ndx++] =
			BROILER_MSR_ENTRY(MSR_CSTAR, 0x00);
	vcpu->msrs->entries[ndx++] =
			BROILER_MSR_ENTRY(MSR_KERNEL_GS_BASE, 0x00);
	vcpu->msrs->entries[ndx++] =
			BROILER_MSR_ENTRY(MSR_SYSCALL_MASK, 0x00);
	vcpu->msrs->entries[ndx++] =
			BROILER_MSR_ENTRY(MSR_LSTAR, 0x00);
	vcpu->msrs->entries[ndx++] =
			BROILER_MSR_ENTRY(MSR_IA32_TSC, 0x00);
	vcpu->msrs->entries[ndx++] =
			BROILER_MSR_ENTRY(MSR_IA32_MISC_ENABLE,
					  MSR_IA32_MISC_ENABLE_FAST_STRING);
	vcpu->msrs->nmsrs = ndx;

	if (ioctl(vcpu->vcpu_fd, KVM_SET_MSRS, vcpu->msrs) < 0)
		die_perror("KVM_SET_MSRS failed");
}

static void broiler_cpu_setup_fpu(struct broiler_cpu *vcpu)
{
	vcpu->fpu = (struct kvm_fpu) {
		.fcw	= 0x37f,
		.mxcsr	= 0x1f80,
	};

	if (ioctl(vcpu->vcpu_fd, KVM_SET_FPU, &vcpu->fpu) < 0)
		die_perror("KVM_SET_FPU failed");
}

static void broiler_cpu_reset_vcpu(struct broiler_cpu *vcpu)
{
	broiler_cpu_setup_cpuid(vcpu);
	broiler_cpu_setup_sregs(vcpu);
	broiler_cpu_setup_regs(vcpu);
	broiler_cpu_setup_fpu(vcpu);
	broiler_cpu_setup_msrs(vcpu);
}

static void broiler_cpu_run(struct broiler_cpu *vcpu)
{
	int err;

	if (!vcpu->is_running)
		return;

	err = ioctl(vcpu->vcpu_fd, KVM_RUN, 0);
	if (err < 0 && (errno != EINTR && errno != EAGAIN))
		die_perror("KVM_RUN failed");
}

static void broiler_cpu_enable_singlestep(struct broiler_cpu *vcpu)
{
	struct kvm_guest_debug debug = {
		.control = KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_SINGLESTEP,
	};

	if (ioctl(vcpu->vcpu_fd, KVM_SET_GUEST_DEBUG, &debug) < 0)
		die("KVM_SET_GUEST_DEBUG failed");
}

static int broiler_cpu_start(struct broiler_cpu *cpu)
{
	int count = 0;

	sigset_t sigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGALRM);

	pthread_sigmask(SIG_BLOCK, &sigset, NULL);

	signal(SIG_BROILER_EXIT, broiler_cpu_signal_handler);
	signal(SIG_BROILER_PAUSE, broiler_cpu_signal_handler);
	signal(SIG_BROILER_TASK, broiler_cpu_signal_handler);

	broiler_cpu_reset_vcpu(cpu);

	while (cpu->is_running) {
		/* KVM RUN */
		broiler_cpu_run(cpu);

		switch (cpu->kvm_run->exit_reason) {
		case KVM_EXIT_UNKNOWN:
			break;	
		case KVM_EXIT_IO : {
			bool ret;

			ret = broiler_cpu_emulate_io(cpu,
						 cpu->kvm_run->io.port,
						 (u8 *)cpu->kvm_run +
						 cpu->kvm_run->io.data_offset,
						 cpu->kvm_run->io.direction,
						 cpu->kvm_run->io.size,
						 cpu->kvm_run->io.count);
			if (!ret)
				goto panic_broiler;
			break;
		}
		case KVM_EXIT_MMIO: {
			bool ret;

			/*
			 * If we had MMIO exit, coalesced ring should be
			 * processed *before* processing the exit itself
			 */
			broiler_cpu_handle_coalesced_mmio(cpu);

			ret = broiler_cpu_emulate_mmio(cpu,
						cpu->kvm_run->mmio.phys_addr,
						cpu->kvm_run->mmio.data,
						cpu->kvm_run->mmio.len,
						cpu->kvm_run->mmio.is_write);
			if (!ret)
				goto panic_broiler;
			break;
		}
		case KVM_EXIT_INTR:
			if (cpu->is_running)
				break;
			goto exit_broiler;
		case KVM_EXIT_SHUTDOWN:
			goto exit_broiler;
		case KVM_EXIT_SYSTEM_EVENT:
			/*
			 * Print the type of system event and
			 * treat all system events as shutdown request.
			 */
			switch (cpu->kvm_run->system_event.type) {
			default:
				printf("unknow system event type %d\n",
					cpu->kvm_run->system_event.type);
				/* fall through for now */
			case KVM_SYSTEM_EVENT_RESET:
				/* fall through for now */
			case KVM_SYSTEM_EVENT_SHUTDOWN:
				/*
				 * Ensure that all VCPUs are torn down,
				 * regardless of which CPU generated the event.
				 */
				broiler_reboot(cpu->broiler);
				goto exit_broiler;
			}
			break;
		default: {
			bool ret;

			ret = broiler_cpu_handle_exit(cpu);
			if (!ret)
				goto panic_broiler;
			break;
		}
		} /* END of switch*/
		broiler_cpu_handle_coalesced_mmio(cpu);
	}

exit_broiler:
	return 0;

panic_broiler:
	return 1;
}

static void *broiler_cpu_thread(void *arg)
{
	char name[20];

	current_broiler_cpu = arg;
	sprintf(name, "Broiler-vcpu-%lu", current_broiler_cpu->cpu_id);
	prctl(PR_SET_NAME, name);

	if (broiler_cpu_start(current_broiler_cpu))
		goto panic_broiler;

	return (void *)(intptr_t) 0;

panic_broiler:
	fprintf(stderr, "*************** Broiler CoreDump ***************\n\n");
	fprintf(stderr, "Broiler exit reason: %u (\"%s\")\n",
		current_broiler_cpu->kvm_run->exit_reason,
	broiler_exit_reasons[current_broiler_cpu->kvm_run->exit_reason]);
	if (current_broiler_cpu->kvm_run->exit_reason ==
						KVM_EXIT_UNKNOWN)
		fprintf(stderr, "Broiler exit reason: 0x%llu\n",
			(unsigned long long)current_broiler_cpu->kvm_run->hw.hardware_exit_reason);
	broiler_cpu_dump_registers(current_broiler_cpu);
	broiler_cpu_dump_code(current_broiler_cpu);	

	return (void *)(intptr_t) 1;
}

int broiler_cpu_running(struct broiler *broiler)
{
	int i;

	for (i = 0; i < broiler->nr_cpu; i++) {
		if (pthread_create(&broiler->cpus[i]->thread, NULL,
				broiler_cpu_thread, broiler->cpus[i]) != 0)
			die("Unable to create KVM VCPU thread");
	}

	/* Only VCPU #0 is going to exit by itself when shutting down */
	if (pthread_join(broiler->cpus[0]->thread, NULL) != 0)
		printf("unable to join with vcpu 0\n");

	return 0;
}

bool kvm_support_vm(void)
{
	struct cpuid_regs regs;
	u32 eax_base;
	int feature;

	regs = (struct cpuid_regs) {
		.eax		= 0x00,
	};
	host_cpuid(&regs);

	switch (regs.ebx) {
	case CPUID_VENDOR_INTEL_1:
		eax_base	= 0x00;
		feature		= KVM_X86_FEATURE_VMX;
		break;
	case CPUID_VENDOR_AMD_1:
		eax_base	= 0x80000000;
		feature		= KVM_X86_FEATURE_SVM;
		break;
	default:
		return false;
	}

	regs = (struct cpuid_regs) {
		.eax		= eax_base,
	};
	host_cpuid(&regs);

	if (regs.eax < eax_base + 0x01)
		return false;

	regs = (struct cpuid_regs) {
		.eax		= eax_base + 0x01,
	};
	host_cpuid(&regs);

	return regs.ecx & (1 << feature);
}

int kvm_init(struct broiler *broiler)
{
	struct kvm_pit_config pit_config = { .flags = 0, };
	struct kvm_userspace_memory_region mem;
	int ret;

	if (kvm_support_vm() < 0) {
		printf("Machine doestn't VM, V-T/V-D enable?\n");
		ret = -ENOSYS;
		goto err_support;
	}

	/* Open KVM */
	broiler->kvm_fd = open("/dev/kvm", O_RDWR);
	if (broiler->kvm_fd < 0) {
		printf("/dev/kvm doesn't exist\n");
		ret = -ENODEV;
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
	
	/* Check KVM extensions */
	if (kvm_check_extensions(broiler) < 0) {
		printf("KVM donesn't support extensions.\n");
		ret = -errno;
		goto err_check_extensions;
	}

	/* Set TSS */
	if (ioctl(broiler->vm_fd, KVM_SET_TSS_ADDR, 0xFFFBD000) < 0) {
		printf("KVM_SET_TSS_ADDR error.\n");
		ret = -errno;
		goto err_set_tss;
	}

	/* Set PIT2 */
	if (ioctl(broiler->vm_fd, KVM_CREATE_PIT2, &pit_config) < 0) {
		printf("KVM_CREATE_PIT2 error.\n");
		ret = -errno;
		goto err_set_pit2;
	}

	/* Memory */
	if (broiler_memory_init(broiler) < 0) {
		printf("Broiler Memory init failed.\n");
		ret = -errno;
		goto err_memory;
	}

	/* IRQ */
	if (ioctl(broiler->vm_fd, KVM_CREATE_IRQCHIP) < 0) {
		printf("KVM_CREATE_IRQCHIP failed\n");
		ret = -errno;
		goto err_create_irqchip;
	}

	return 0;

err_create_irqchip:
	broiler_memory_exit(broiler);
err_memory:
err_set_pit2:
err_set_tss:
err_check_extensions:
err_create_vm:
err_kvm_version:
	close(broiler->kvm_fd);
err_open_kvm:
err_support:
	return ret;
}

void kvm_exit(struct broiler *broiler)
{
	broiler_memory_exit(broiler);
	close(broiler->kvm_fd);
}
