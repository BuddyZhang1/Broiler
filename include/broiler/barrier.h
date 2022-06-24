#ifndef _BROILER_BARRIER_H
#define _BROILER_BARRIER_H

#define barrier()	asm volatile ("" ::: "memory")
#define mb()		asm volatile ("mfence" ::: "memory")
#define rmb()		asm volatile ("lfence" ::: "memory")
#define wmb()		asm volatile ("sfence" ::: "memory")

#endif
