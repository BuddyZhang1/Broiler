// SPDX-License-Identifier: GPL-2.0-only
#ifndef _BROILER_MUTEX_H
#define _BROILER_MUTEX_H

#include "broiler/broiler.h"
#include <pthread.h>

struct mutex {
	pthread_mutex_t mutex;
};

#define MUTEX_INITIALIZER { .mutex = PTHREAD_MUTEX_INITIALIZER }

#define DEFINE_MUTEX(mtx) struct mutex mtx = MUTEX_INITIALIZER

static inline void mutex_init(struct mutex *lock)
{
	if (pthread_mutex_init(&lock->mutex, NULL) != 0) {
		printf("unexpected pthread_mutex_init() failure!");
		exit(-1);
	}
}

static inline void mutex_lock(struct mutex *lock)
{
	if (pthread_mutex_lock(&lock->mutex) != 0) {
		printf("unexpected pthread_mutex_lock() failure!");
		exit(-1);
	}
}

static inline void mutex_unlock(struct mutex *lock)
{
	if (pthread_mutex_unlock(&lock->mutex) != 0) {
		printf("unexpected pthread_mutex_unlock() failure!");
		exit(-1);
	}
}


#endif
