#ifndef _BROILER_THREADPOOL_H
#define _BROILER_THREADPOOL_H

#include "broiler/broiler.h"
#include "linux/list.h"
#include "linux/mutex.h"

typedef void (*broiler_thread_callback_fn_t)(struct broiler *broiler, void *data);

struct thread_pool_job {
	broiler_thread_callback_fn_t	callback;
	struct broiler			*broiler;
	void				*data;

	int				signalcount;
	struct mutex			mutex;

	struct list_head		queue;
};

#endif
