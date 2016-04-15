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


struct upollfd;

struct upoll
{
	struct upollfd *fd;
	void (*read)(void *);
	void (*write)(void *);
	void (*hangup)(void *);
	void *closure;
	struct upoll *next;
};

struct upollfd
{
	int fd;
	uint32_t events;
	struct upollfd *next;
	struct upoll *head;
};

static int pollfd = 0;
static struct upollfd *head = NULL;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static int update(struct upollfd *ufd)
{
	int rc;
	struct upoll *u;
	struct epoll_event e;
	uint32_t events;
	struct upollfd **prv;

	events = 0;
	pthread_mutex_lock(&mutex);
	u = ufd->head;
	if (u == NULL) {
		/* no more watchers */
		prv = &head;
		while(*prv) {
			if (*prv == ufd) {
				*prv = ufd->next;
				break;
			}
			prv = &(*prv)->next;
		}
		pthread_mutex_unlock(&mutex);
		epoll_ctl(pollfd, EPOLL_CTL_DEL, ufd->fd, NULL);
		free(ufd);
		return 0;
	}
	/* compute the events for the watchers */
	while (u != NULL) {
		if (u->read != NULL)
			events |= EPOLLIN;
		if (u->write != NULL)
			events |= EPOLLOUT;
		u = u->next;
	}
	pthread_mutex_unlock(&mutex);
	if (ufd->events == events)
		return 0;
	e.events = events;
	e.data.ptr = ufd;
	rc = epoll_ctl(pollfd, EPOLL_CTL_MOD, ufd->fd, &e);
	if (rc == 0)
		ufd->events = events;
	return rc;
}

static struct upollfd *get_fd(int fd)
{
	struct epoll_event e;
	struct upollfd *result;
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

	/* search */
	result = head;
	while (result != NULL) {
		if (result->fd == fd)
			return result;
		result = result->next;
	}

	/* allocates */
	result = calloc(1, sizeof *result);
	if (result == NULL)
		return NULL;

	/* init */
	result->fd = fd;
	pthread_mutex_lock(&mutex);
	result->next = head;
	head = result;
	pthread_mutex_unlock(&mutex);

	/* records */
	e.events = 0;
	e.data.ptr = result;
	rc = epoll_ctl(pollfd, EPOLL_CTL_ADD, fd, &e);
	if (rc == 0)
		return result;

	/* revert on error */
	rc = errno;
	update(result);
	errno = rc;
	return NULL;
}

int upoll_is_valid(struct upoll *upoll)
{
	struct upollfd *itfd = head;
	struct upoll *it;
	while (itfd != NULL) {
		it = itfd->head;
		while (it != NULL) {
			if (it == upoll)
				return 1;
			it = it->next;
		}
		itfd = itfd->next;
	}
	return 0;
}

struct upoll *upoll_open(int fd, void *closure)
{
	struct upollfd *ufd;
	struct upoll *result;

	/* allocates */
	result = calloc(1, sizeof *result);
	if (result == NULL)
		return NULL;

	/* get for fd */
	ufd = get_fd(fd);
	if (ufd == NULL) {
		free(result);
		return NULL;
	}

	/* init */
	result->fd = ufd;
	result->closure = closure;
	pthread_mutex_lock(&mutex);
	result->next = ufd->head;
	ufd->head = result;
	pthread_mutex_unlock(&mutex);
	return result;
}

int upoll_on_readable(struct upoll *upoll, void (*process)(void *))
{
	assert(pollfd != 0);
	assert(upoll_is_valid(upoll));

	upoll->read = process;
	return update(upoll->fd);
}

int upoll_on_writable(struct upoll *upoll, void (*process)(void *))
{
	assert(pollfd != 0);
	assert(upoll_is_valid(upoll));

	upoll->write = process;
	return update(upoll->fd);
}

void upoll_on_hangup(struct upoll *upoll, void (*process)(void *))
{
	assert(pollfd != 0);
	assert(upoll_is_valid(upoll));

	upoll->hangup = process;
}

void upoll_close(struct upoll *upoll)
{
	struct upoll **it;
	struct upollfd *ufd;

	assert(pollfd != 0);
	assert(upoll_is_valid(upoll));

	ufd = upoll->fd;
	pthread_mutex_lock(&mutex);
	it = &ufd->head;
	while (*it != upoll)
		it = &(*it)->next;
	*it = upoll->next;
	pthread_mutex_unlock(&mutex);
	free(upoll);
	update(ufd);
}

int upoll_wait(int timeout)
{
	int rc;
	struct epoll_event e;
	struct upollfd *ufd;
	struct upoll *u;

	if (pollfd == 0) {
		errno = ECANCELED;
		return -1;
	}

	do {
		rc = epoll_wait(pollfd, &e, 1, timeout);
	} while (rc < 0 && errno == EINTR);
	if (rc == 1) {
		ufd = e.data.ptr;
		u = ufd->head;
		while (u != NULL) {
			if ((e.events & EPOLLIN) && u->read) {
				u->read(u->closure);
				break;
			}
			if ((e.events & EPOLLOUT) && u->write) {
				u->write(u->closure);
				break;
			}
			if ((e.events & EPOLLHUP) && u->hangup) {
				u->hangup(u->closure);
				break;
			}
			u = u->next;
		}
	}
	return rc < 0 ? rc : 0;
}

