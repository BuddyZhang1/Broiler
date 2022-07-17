// SPDX-License-Identifier: GPL-2.0-only
#include "broiler/threadpool.h"
#include "linux/mutex.h"
#include <sys/prctl.h>

static DEFINE_MUTEX(thread_mutex);
static DEFINE_MUTEX(job_mutex);
static pthread_t *broiler_threads;
static long broiler_threadcount;
static bool broiler_running;
static LIST_HEAD(broiler_head);
static pthread_cond_t job_cond = PTHREAD_COND_INITIALIZER;
static pthread_t *broiler_threads;

static struct thread_pool_job *thread_pool_job_pop_locked(void)
{
	struct thread_pool_job *job;

	if (list_empty(&broiler_head))
		return NULL;

	job = list_first_entry(&broiler_head, struct thread_pool_job, queue);
	list_del_init(&job->queue);

	return job;
}

static void thread_pool_job_push_locked(struct thread_pool_job *job)
{
	list_add_tail(&job->queue, &broiler_head);
}

static void thread_pool_threadfunc_cleanup(void *param)
{
	mutex_unlock(&job_mutex);
}

static void thread_pool_job_push(struct thread_pool_job *job)
{
	mutex_lock(&job_mutex);
	thread_pool_job_push_locked(job);
	mutex_unlock(&job_mutex);
}

static struct thread_pool_job *thread_pool_job_pop(void)
{
	struct thread_pool_job *job;

	mutex_lock(&job_mutex);
	job = thread_pool_job_pop_locked();
	mutex_unlock(&job_mutex);
	return job;
}

static void thread_pool_handle_job(struct thread_pool_job *job)
{
	while (job) {
		job->callback(job->broiler, job->data);
		mutex_lock(&job->mutex);

		if (--job->signalcount > 0) {
			/* If the job was signaled again while 
			 * we were working */
			thread_pool_job_push(job);
		}
		mutex_unlock(&job->mutex);
		job = thread_pool_job_pop();
	}
}

static void *thread_pool_threadfunc(void *param)
{
	pthread_cleanup_push(thread_pool_threadfunc_cleanup, NULL);
	prctl(PR_SET_NAME, "threadpool-worker");

	while (broiler_running) {
		struct thread_pool_job *current_job = NULL;

		mutex_lock(&job_mutex);
		while (broiler_running && 
			(current_job = thread_pool_job_pop_locked()) == NULL)
			pthread_cond_wait(&job_cond, &job_mutex.mutex);
		mutex_unlock(&job_mutex);

		if (broiler_running)
			thread_pool_handle_job(current_job);
	}

	pthread_cleanup_pop(0);
	return NULL;
}

static int thread_pool_addthread(void)
{
	void *newthreads;
	int res;

	mutex_lock(&thread_mutex);
	newthreads = realloc(broiler_threads,
			(broiler_threadcount + 1) * sizeof(pthread_t));
	if (!newthreads) {
		mutex_unlock(&thread_mutex);
		return -1;
	}

	broiler_threads = newthreads;

	res = pthread_create(broiler_threads + broiler_threadcount,
					NULL, thread_pool_threadfunc, NULL);
	if (res == 0)
		broiler_threadcount++;
	mutex_unlock(&thread_mutex);

	return res;
}

int broiler_threadpool_init(struct broiler *broiler)
{
	unsigned int thread_count = sysconf(_SC_NPROCESSORS_ONLN);
	unsigned long i;

	broiler_running = true;

	for (i = 0; i < thread_count; i++)
		if (thread_pool_addthread() < 0)
			return i;

	return i;
}

int broiler_threadpool_exit(struct broiler *broiler)
{
	int i;

	broiler_running = false;

	for (i = 0; i < broiler_threadcount; i++) {
		mutex_lock(&job_mutex);
		pthread_cond_signal(&job_cond);
		mutex_unlock(&job_mutex);
	}

	for (i = 0; i < broiler_threadcount; i++)
		pthread_join(broiler_threads[i], NULL);

	return 0;
}
