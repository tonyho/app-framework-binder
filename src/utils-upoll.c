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

/*
 * Structure describing one opened client
 */
struct upoll
{
	struct upollfd *fd;	/* structure handling the file descriptor */
	void (*read)(void *);	/* callback for handling on_readable */
	void (*write)(void *);	/* callback for handling on_writable */
	void (*hangup)(void *);	/* callback for handling on_hangup */
	void *closure;		/* closure for callbacks */
	struct upoll *next; 	/* next client of the same file descriptor */
};

/*
 * Structure describing a watched file descriptor
 */
struct upollfd
{
	int fd;			/* watch file descriptor */
	uint32_t events;	/* watched events */
	struct upollfd *next;	/* next watched file descriptor */
	struct upoll *head;	/* first client watching the file descriptor */
};


/*
 * Structure describing a upoll group
struct upollgrp
{
	int pollfd;
	struct upollfd *head;
	struct upoll *current;
	pthread_mutex_t mutex;
};


static struct upollgrp global = {
	.pollfd = 0,
	.head = NULL,
	.current = NULL,
	.mutex = PTHREAD_MUTEX_INITIALIZER
};
 */

static int pollfd = 0;
static struct upollfd *head = NULL;
static struct upoll *current = NULL;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Compute the events for the set of clients
 */
static int update_flags_locked(struct upollfd *ufd)
{
	int rc;
	struct upoll *u;
	struct epoll_event e;
	uint32_t events;

	/* compute expected events */
	events = 0;
	u = ufd->head;
	while (u != NULL) {
		if (u->read != NULL)
			events |= EPOLLIN;
		if (u->write != NULL)
			events |= EPOLLOUT;
		u = u->next;
	}
	if (ufd->events == events)
		rc = 0;
	else {
		e.events = events;
		e.data.ptr = ufd;
		rc = epoll_ctl(pollfd, EPOLL_CTL_MOD, ufd->fd, &e);
		if (rc == 0)
			ufd->events = events;
	}
	pthread_mutex_unlock(&mutex);
	return rc;
}

/*
 * Compute the events for the set of clients
 */
static int update_flags(struct upollfd *ufd)
{
	pthread_mutex_lock(&mutex);
	return update_flags_locked(ufd);
}

/*
 *
 */
static int update(struct upollfd *ufd)
{
	struct upollfd **prv;

	pthread_mutex_lock(&mutex);
	if (ufd->head != NULL)
		return update_flags_locked(ufd);

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
	return update_flags(upoll->fd);
}

int upoll_on_writable(struct upoll *upoll, void (*process)(void *))
{
	assert(pollfd != 0);
	assert(upoll_is_valid(upoll));

	upoll->write = process;
	return update_flags(upoll->fd);
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
	if (current == upoll)
		current = NULL;
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

	if (pollfd == 0) {
		errno = ECANCELED;
		return -1;
	}

	do {
		rc = epoll_wait(pollfd, &e, 1, timeout);
	} while (rc < 0 && errno == EINTR);
	if (rc == 1) {
		ufd = e.data.ptr;
		current = ufd->head;
		e.events &= EPOLLIN | EPOLLOUT | EPOLLHUP;
		while (current != NULL && e.events != 0) {
			if ((e.events & EPOLLIN) && current->read) {
				current->read(current->closure);
				e.events &= (uint32_t)~EPOLLIN;
				continue;
			}
			if ((e.events & EPOLLOUT) && current->write) {
				current->write(current->closure);
				e.events &= (uint32_t)~EPOLLOUT;
				continue;
			}
			if ((e.events & EPOLLHUP) && current->hangup) {
				current->hangup(current->closure);
				if (current == NULL)
					break;
			}
			current = current->next;
		}
	}
	return rc < 0 ? rc : 0;
}

