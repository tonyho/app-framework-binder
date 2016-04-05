/*
 * Copyright 2016 IoT.bzh
 * Author: José Bollo <jose.bollo@iot.bzh>
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

#include <sys/epoll.h>
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

#include "utils-upoll.h"


struct upoll
{
	int fd;
	void (*process)(void *closure, int fd, uint32_t events);
	void *closure;
	struct upoll *next;
};

static int pollfd = 0;
static struct upoll *head = NULL;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int upoll_is_valid(struct upoll *upoll)
{
	struct upoll *it = head;
	while (it != NULL) {
		if (it == upoll)
			return 1;
		it = it->next;
	}
	return 0;
}

struct upoll *upoll_open(int fd, uint32_t events, void (*process)(void *closure, int fd, uint32_t events), void *closure)
{
	struct epoll_event e;
	struct upoll *result;
	int rc;

	/* opens the epoll stream */
	if (pollfd == 0) {
		pollfd = epoll_create1(EPOLL_CLOEXEC);
		if (pollfd == 0) {
			pollfd = dup(0);
			close(0);
		}
		if (pollfd < 0) {
			pollfd = 0;
			return NULL;
		}
	}

	/* allocates */
	result = malloc(sizeof *result);
	if (result == NULL)
		return NULL;

	/* init */
	result->fd = fd;
	result->process = process;
	result->closure = closure;
	pthread_mutex_lock(&mutex);
	result->next = head;
	head = result;
	pthread_mutex_unlock(&mutex);

	/* records */
	e.events = events;
	e.data.ptr = result;
	rc = epoll_ctl(pollfd, EPOLL_CTL_ADD, fd, &e);
	if (rc == 0)
		return result;

	/* revert on error */
	rc = errno;
	upoll_close(result);
	errno = rc;
	return NULL;
}

int upoll_update(struct upoll *upoll, uint32_t events)
{
	struct epoll_event e;

	assert(pollfd != 0);
	assert(upoll_is_valid(upoll));

	e.events = events;
	e.data.ptr = upoll;
	return epoll_ctl(pollfd, EPOLL_CTL_MOD, upoll->fd, &e);
}

void upoll_close(struct upoll *upoll)
{
	struct upoll **it;

	assert(pollfd != 0);
	assert(upoll_is_valid(upoll));

	epoll_ctl(pollfd, EPOLL_CTL_DEL, upoll->fd, NULL);
	pthread_mutex_lock(&mutex);
	it = &head;
	while (*it != upoll)
		it = &(*it)->next;
	*it = upoll->next;
	pthread_mutex_unlock(&mutex);
	free(upoll);
}

void upoll_wait(int timeout)
{
	int rc;
	struct epoll_event e;
	struct upoll *upoll;

	if (pollfd == 0)
		return;

	rc = epoll_wait(pollfd, &e, 1, timeout);
	if (rc == 1) {
		upoll = e.data.ptr;
		upoll->process(upoll->closure, upoll->fd, e.events);
	}
}

