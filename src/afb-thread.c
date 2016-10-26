/*
 * Copyright (C) 2016 "IoT.bzh"
 * Author José Bollo <jose.bollo@iot.bzh>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <errno.h>
#include <assert.h>

#include <afb/afb-req-itf.h>

#include "afb-thread.h"
#include "afb-sig-handler.h"
#include "verbose.h"

/* control of threads */
struct thread
{
	pthread_t tid;     /* the thread id */
	unsigned stop: 1;  /* stop request */
	unsigned ended: 1; /* ended status */
	unsigned works: 1; /* is it processing a job? */
};

/* describes pending job */
struct job
{
	void (*callback)(struct afb_req req); /* processing callback */
	struct afb_req req; /* request to be processed */
	int timeout;        /* timeout in second for processing the request */
	int blocked;        /* is an other request blocking this one ? */
	void *group;        /* group of the request */
	struct job *next;   /* link to the next job enqueued */
};

/* synchronisation of threads */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cond = PTHREAD_COND_INITIALIZER;

/* queue of pending jobs */
static struct job *first_job = NULL;

/* count allowed, started and running threads */
static int allowed = 0;
static int started = 0;
static int running = 0;
static int remains = 0;

/* list of threads */
static struct thread *threads = NULL;

/* local timers */
static _Thread_local int thread_timer_set;
static _Thread_local timer_t thread_timerid;

/*
 * Creates a timer for the current thread
 *
 * Returns 0 in case of success
 */
int afb_thread_timer_create()
{
	int rc;
	struct sigevent sevp;

	if (thread_timer_set)
		rc = 0;
	else {
		sevp.sigev_notify = SIGEV_THREAD_ID;
		sevp.sigev_signo = SIGALRM;
		sevp.sigev_value.sival_ptr = NULL;
#if defined(sigev_notify_thread_id)
		sevp.sigev_notify_thread_id = (pid_t)syscall(SYS_gettid);
#else
		sevp._sigev_un._tid = (pid_t)syscall(SYS_gettid);
#endif
		rc = timer_create(CLOCK_THREAD_CPUTIME_ID, &sevp, &thread_timerid);
		thread_timer_set = !rc;
	}
	return 0;
}

/*
 * Arms the alarm in timeout seconds for the current thread
 */
int afb_thread_timer_arm(int timeout)
{
	int rc;
	struct itimerspec its;

	rc = afb_thread_timer_create();
	if (rc == 0) {
		its.it_interval.tv_sec = 0;
		its.it_interval.tv_nsec = 0;
		its.it_value.tv_sec = timeout;
		its.it_value.tv_nsec = 0;
		rc = timer_settime(thread_timerid, 0, &its, NULL);
	}

	return rc;
}

/*
 * Disarms the current alarm
 */
void afb_thread_timer_disarm()
{
	if (thread_timer_set)
		afb_thread_timer_arm(0);
}

/*
 * Delstroy any alarm resource for the current thread
 */
void afb_thread_timer_delete()
{
	if (thread_timer_set) {
		timer_delete(thread_timerid);
		thread_timer_set = 0;
	}
}

/* add the job to the list */
static inline void job_add(struct job *job)
{
	void *group = job->group;
	struct job *ijob, **pjob;

	pjob = &first_job;
	ijob = first_job;
	group = job->group;
	if (group == NULL)
		group = job;
	while (ijob) {
		if (ijob->group == group)
			job->blocked = 1;
		pjob = &ijob->next;
		ijob = ijob->next;
	}
	*pjob = job;
	job->next = NULL;
	remains--;
}

/* get the next job to process or NULL if none */
static inline struct job *job_get()
{
	struct job *job, **pjob;
	pjob = &first_job;
	job = first_job;
	while (job && job->blocked) {
		pjob = &job->next;
		job = job->next;
	}
	if (job) {
		*pjob = job->next;
		remains++;
	}
	return job;
}

/* unblock a group of job */
static inline void job_unblock(void *group)
{
	struct job *job;

	job = first_job;
	while (job) {
		if (job->group == group) {
			job->blocked = 0;
			break;
		}
		job = job->next;
	}
}

/* main loop of processing threads */
static void *thread_main_loop(void *data)
{
	struct thread *me = data;
	struct job *job, j;

	me->works = 0;
	me->ended = 0;
	afb_thread_timer_create();
	pthread_mutex_lock(&mutex);
	while (!me->stop) {
		/* get a job */
		job = job_get();
		if (job == NULL && first_job != NULL && running == 0) {
			/* sad situation!! should not happen */
			ERROR("threads are blocked!");
			job = first_job;
			first_job = job->next;
		}
		if (job == NULL) {
			/* no job... */
			pthread_cond_wait(&cond, &mutex);
		} else {
			/* run the job */
			running++;
			me->works = 1;
			pthread_mutex_unlock(&mutex);
			j = *job;
			free(job);
			afb_thread_timer_arm(j.timeout);
			afb_sig_req(j.req, j.callback);
			afb_thread_timer_disarm();
			afb_req_unref(j.req);
			pthread_mutex_lock(&mutex);
			if (j.group != NULL)
				job_unblock(j.group);
			me->works = 0;
			running--;
		}

	}
	me->ended = 1;
	pthread_mutex_unlock(&mutex);
	afb_thread_timer_delete();
	return me;
}

/* start a new thread */
static int start_one_thread()
{
	struct thread *t;
	int rc;

	assert(started < allowed);

	t = &threads[started++];
	t->stop = 0;
	rc = pthread_create(&t->tid, NULL, thread_main_loop, t);
	if (rc != 0) {
		started--;
		errno = rc;
		WARNING("not able to start thread: %m");
		rc = -1;
	}
	return rc;
}

/* process the 'request' with the 'callback' using a separate thread if available */
void afb_thread_call(struct afb_req req, void (*callback)(struct afb_req req), int timeout, void *group)
{
	const char *info;
	struct job *job;
	int rc;

	/* allocates the job */
	job = malloc(sizeof *job);
	if (job == NULL) {
		info = "out of memory";
		goto error;
	}

	/* start a thread if needed */
	pthread_mutex_lock(&mutex);
	if (remains == 0) {
		info = "too many jobs";
		goto error2;
	}
	if (started == running && started < allowed) {
		rc = start_one_thread();
		if (rc < 0 && started == 0) {
			/* failed to start threading */
			info = "can't start thread";
			goto error2;
		}
	}

	/* fills and queues the job */
	job->callback = callback;
	job->req = req;
	job->timeout = timeout;
	job->blocked = 0;
	job->group = group;
	afb_req_addref(req);
	job_add(job);
	pthread_mutex_unlock(&mutex);

	/* signal an existing job */
	pthread_cond_signal(&cond);
	return;

error2:
	pthread_mutex_unlock(&mutex);
	free(job);
error:
	ERROR("can't process job with threads: %s", info);
	afb_req_fail(req, "internal-error", info);
}

/* initialise the threads */
int afb_thread_init(int allowed_count, int start_count, int waiter_count)
{
	threads = calloc(allowed_count, sizeof *threads);
	if (threads == NULL) {
		errno = ENOMEM;
		ERROR("can't allocate threads");
		return -1;
	}

	/* records the allowed count */
	allowed = allowed_count;
	started = 0;
	running = 0;
	remains = waiter_count;

	/* start at least one thread */
	pthread_mutex_lock(&mutex);
	while (started < start_count && start_one_thread() == 0);
	pthread_mutex_unlock(&mutex);

	/* end */
	return -(started != start_count);
}

/* terminate all the threads and all pending requests */
void afb_thread_terminate()
{
	int i, n;
	struct job *job;

	/* request all threads to stop */
	pthread_mutex_lock(&mutex);
	allowed = 0;
	n = started;
	for (i = 0 ; i < n ; i++)
		threads[i].stop = 1;

	/* wait until all thread are terminated */
	while (started != 0) {
		/* signal threads */
		pthread_mutex_unlock(&mutex);
		pthread_cond_broadcast(&cond);
		pthread_mutex_lock(&mutex);

		/* join the terminated threads */
		for (i = 0 ; i < n ; i++) {
			if (threads[i].tid && threads[i].ended) {
				pthread_join(threads[i].tid, NULL);
				threads[i].tid = 0;
				started--;
			}
		}
	}
	pthread_mutex_unlock(&mutex);
	free(threads);

	/* cancel pending jobs */
	while (first_job) {
		job = first_job;
		first_job = job->next;
		afb_req_fail(job->req, "aborted", "termination of threading");
		afb_req_unref(job->req);
		free(job);
	}
}
