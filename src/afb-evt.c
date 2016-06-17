/*
 * Copyright (C) 2015, 2016 "IoT.bzh"
 * Author "Fulup Ar Foll"
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
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <json-c/json.h>
#include <afb/afb-event-itf.h>

#include "afb-evt.h"


struct afb_evt_watch;

struct afb_evt_listener {
	struct afb_evt_listener *next;
	void (*send)(void *closure, const char *event, struct json_object *object);
	void *closure;
	struct afb_evt_watch *watchs;
	int refcount;
};

struct afb_evt_event {
	struct afb_evt_watch *watchs;
	char name[1];
};

struct afb_evt_watch {
	struct afb_evt_event *event;
	struct afb_evt_watch *next_by_event;
	struct afb_evt_listener *listener;
	struct afb_evt_watch *next_by_listener;
};

static int evt_broadcast(struct afb_evt_event *evt, struct json_object *obj);
static int evt_push(struct afb_evt_event *evt, struct json_object *obj);
static void evt_drop(struct afb_evt_event *evt);

static struct afb_event_itf afb_evt_event_itf = {
	.broadcast = (void*)evt_broadcast,
	.push = (void*)evt_push,
	.drop = (void*)evt_drop
};

static struct afb_evt_listener *listeners = NULL;

static int evt_broadcast(struct afb_evt_event *evt, struct json_object *object)
{
	return afb_evt_broadcast(evt->name, object);
}

int afb_evt_broadcast(const char *event, struct json_object *object)
{
	int result;
	struct afb_evt_listener *listener;

	result = 0;
	listener = listeners;
	while(listener) {
		listener->send(listener->closure, event, json_object_get(object));
		listener = listener->next;
	}
	json_object_put(object);
	return result;
}

static int evt_push(struct afb_evt_event *evt, struct json_object *obj)
{
	int result;
	struct afb_evt_watch *watch;
	struct afb_evt_listener *listener;

	result = 0;
	watch = evt->watchs;
	while(listener) {
		listener = watch->listener;
		listener->send(listener->closure, evt->name, json_object_get(obj));
		watch = watch->next_by_event;
	}
	json_object_put(obj);
	return result;
}

static void remove_watch(struct afb_evt_watch *watch)
{
	struct afb_evt_watch **prv;

	prv = &watch->event->watchs;
	while(*prv != watch)
		prv = &(*prv)->next_by_event;
	*prv = watch->next_by_event;

	prv = &watch->listener->watchs;
	while(*prv != watch)
		prv = &(*prv)->next_by_listener;
	*prv = watch->next_by_listener;

	free(watch);
}

static void evt_drop(struct afb_evt_event *evt)
{
	if (evt != NULL) {
		while(evt->watchs != NULL)
			remove_watch(evt->watchs);
		free(evt);
	}
}

struct afb_event afb_evt_create_event(const char *name)
{
	size_t len;
	struct afb_evt_event *evt;

	len = strlen(name);
	evt = malloc(len + sizeof * evt);
	if (evt != NULL) {
		evt->watchs = NULL;
		memcpy(evt->name, name, len + 1);
	}
	return (struct afb_event){ .itf = &afb_evt_event_itf, .closure = evt };
}

struct afb_evt_listener *afb_evt_listener_create(void (*send)(void *closure, const char *event, struct json_object *object), void *closure)
{
	struct afb_evt_listener *listener;

	/* search if an instance already exists */
	listener = listeners;
	while (listener != NULL) {
		if (listener->send == send && listener->closure == closure)
			return afb_evt_listener_addref(listener);
		listener = listener->next;
	}

	/* allocates */
	listener = calloc(1, sizeof *listener);
	if (listener != NULL) {
		/* init */
		listener->next = listeners;
		listener->send = send;
		listener->closure = closure;
		listener->watchs = NULL;
		listener->refcount = 1;
		listeners = listener;
	}
	return listener;
}

struct afb_evt_listener *afb_evt_listener_addref(struct afb_evt_listener *listener)
{
	listener->refcount++;
	return listener;
}

void afb_evt_listener_unref(struct afb_evt_listener *listener)
{
	if (0 == --listener->refcount) {
		struct afb_evt_listener **prv;

		/* remove the watchers */
		while (listener->watchs != NULL)
			remove_watch(listener->watchs);

		/* unlink the listener */
		prv = &listeners;
		while (*prv != listener)
			prv = &(*prv)->next;
		*prv = listener->next;

		/* free the listener */
		free(listener);
	}
}

int afb_evt_add_watch(struct afb_evt_listener *listener, struct afb_event event)
{
	struct afb_evt_watch *watch;
	struct afb_evt_event *evt;

	/* check parameter */
	if (event.itf != &afb_evt_event_itf) {
		errno = EINVAL;
		return -1;
	}

	/* search the existing watch */
	watch = listener->watchs;
	while(watch != NULL) {
		if (watch->event == event.closure)
			return 0;
		watch = watch->next_by_listener;
	}

	/* not found, allocate a new */
	watch = malloc(sizeof *watch);
	if (watch == NULL) {
		errno = ENOMEM;
		return -1;
	}

	/* initialise and link */
	evt = event.closure;
	watch->event = evt;
	watch->next_by_event = evt->watchs;
	watch->listener = listener;
	watch->next_by_listener = listener->watchs;
	evt->watchs = watch;
	listener->watchs = watch;
	
	return 0;
}

int afb_evt_remove_watch(struct afb_evt_listener *listener, struct afb_event event)
{
	struct afb_evt_watch *watch;

	/* check parameter */
	if (event.itf != &afb_evt_event_itf) {
		errno = EINVAL;
		return -1;
	}

	/* search the existing watch */
	watch = listener->watchs;
	while(watch != NULL) {
		if (watch->event == event.closure) {
			/* found: remove it */
			remove_watch(watch);
			break;
		}
		watch = watch->next_by_listener;
	}
	return 0;
}


