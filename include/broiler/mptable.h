// SPDX-License-Identifier: GPL-2.0-only
#ifndef _BROILER_MPTABLE_H
#define _BROILER_MPTABLE_H

/* It should be more than enough */
#define MPTABLE_MAX_SIZE	(32 << 20)

#define MPTABLE_STRNCPY(d, s)	memcpy(d, s, sizeof(d))

#define MPTABLE_SIG_FLOATING	"_MP_"
#define MPC_SIGNATURE		"PCMP"
#define MPTABLE_OEM		"BROILERCPU00"
#define MPTABLE_PRODUCTID	"0.11            "
#define MPTABLE_PCIBUSTYPE	"PCI    "
#define MPTABLE_ISABUSTYPE	"ISA    "

/* Followed by entries */

#define MP_PROCESSOR		0
#define MP_BUS			1
#define MP_IOAPIC		2
#define MP_INTSRC		3
#define MP_LINTSRC		4

#define CPU_ENABLED		1       /* Processor is available */
#define CPU_BOOTPROCESSOR	2       /* Processor is the BP */

#define MPC_APIC_USABLE		0x01

#define MP_IRQDIR_DEFAULT	0
#define MP_IRQDIR_HIGH		1
#define MP_IRQDIR_LOW		3

#define MP_APIC_ALL		0xFF

struct mpc_table {
	char signature[4];
	unsigned short length;          /* Size of table */
	char spec;                      /* 0x01 */
	char checksum;
	char oem[8];
	char productid[12];
	unsigned int oemptr;            /* 0 if not present */
	unsigned short oemsize;         /* 0 if not present */
	unsigned short oemcount;
	unsigned int lapic;             /* APIC address */
	unsigned int reserved;
};

struct mpc_cpu {
	unsigned char type;
	unsigned char apicid;           /* Local APIC number */
	unsigned char apicver;          /* Its versions */
	unsigned char cpuflag;
	unsigned int cpufeature;
	unsigned int featureflag;       /* CPUID feature value */
	unsigned int reserved[2];
};

struct mpc_bus {
	unsigned char type;
	unsigned char busid;
	unsigned char bustype[6];
};

struct mpc_ioapic {
	unsigned char type;
	unsigned char apicid;
	unsigned char apicver;
	unsigned char flags;
	unsigned int apicaddr;
};

struct mpc_intsrc { 
	unsigned char type;
	unsigned char irqtype;
	unsigned short irqflag;
	unsigned char srcbus;
	unsigned char srcbusirq;
	unsigned char dstapic;
	unsigned char dstirq;
};

enum mp_irq_source_types {
	MP_INT = 0,
	MP_NMI = 1,
	MP_SMI = 2,
	MP_ExtINT = 3
};

/* Intel MP Floating Pointer Structure */
struct mpf_intel {
	char signature[4];              /* "_MP_"                       */
	unsigned int physptr;           /* Configuration table address  */
	unsigned char length;           /* Our length (paragraphs)      */
	unsigned char specification;    /* Specification version        */
	unsigned char checksum;         /* Checksum (makes sum 0)       */
	unsigned char feature1;         /* Standard or configuration ?  */
	unsigned char feature2;         /* Bit7 set for IMCR|PIC        */
	unsigned char feature3;         /* Unused (0)                   */
	unsigned char feature4;         /* Unused (0)                   */
	unsigned char feature5;         /* Unused (0)                   */
};

#endif
