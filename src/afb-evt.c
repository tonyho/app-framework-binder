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

/*
 * Structure for event listeners
 */
struct afb_evt_listener {

	/* chaining listeners */
	struct afb_evt_listener *next;

	/* interface for callbacks */
	const struct afb_evt_itf *itf;

	/* closure for the callback */
	void *closure;

	/* head of the list of events listened */
	struct afb_evt_watch *watchs;

	/* count of reference to the listener */
	int refcount;
};

/*
 * Structure for describing events
 */
struct afb_evt_event {

	/* next event */
	struct afb_evt_event *next;

	/* head of the list of listeners watching the event */
	struct afb_evt_watch *watchs;

	/* id of the event */
	int id;

	/* name of the event */
	char name[1];
};

/*
 * Structure for associating events and listeners
 */
struct afb_evt_watch {

	/* the event */
	struct afb_evt_event *event;

	/* link to the next listener for the same event */
	struct afb_evt_watch *next_by_event;

	/* the listener */
	struct afb_evt_listener *listener;

	/* link to the next event for the same listener */
	struct afb_evt_watch *next_by_listener;

	/* activity */
	unsigned activity;
};

/* declare functions */
static int evt_broadcast(struct afb_evt_event *evt, struct json_object *obj);
static int evt_push(struct afb_evt_event *evt, struct json_object *obj);
static void evt_destroy(struct afb_evt_event *evt);
static const char *evt_name(struct afb_evt_event *evt);

/* the interface for events */
static struct afb_event_itf afb_evt_event_itf = {
	.broadcast = (void*)evt_broadcast,
	.push = (void*)evt_push,
	.drop = (void*)evt_destroy,
	.name = (void*)evt_name
};

/* head of the list of listeners */
static struct afb_evt_listener *listeners = NULL;

/* handling id of events */
static struct afb_evt_event *events = NULL;
static int event_id_counter = 0;
static int event_id_wrapped = 0;

/*
 * Broadcasts the event 'evt' with its 'object'
 * 'object' is released (like json_object_put)
 * Returns the count of listener that received the event.
 */
static int evt_broadcast(struct afb_evt_event *evt, struct json_object *object)
{
	return afb_evt_broadcast(evt->name, object);
}

/*
 * Broadcasts the 'event' with its 'object'
 * 'object' is released (like json_object_put)
 * Returns the count of listener having receive the event.
 */
int afb_evt_broadcast(const char *event, struct json_object *object)
{
	int result;
	struct afb_evt_listener *listener;

	result = 0;
	listener = listeners;
	while(listener) {
		if (listener->itf->broadcast != NULL) {
			listener->itf->broadcast(listener->closure, event, 0, json_object_get(object));
			result++;
		}
		listener = listener->next;
	}
	json_object_put(object);
	return result;
}

/*
 * Pushes the event 'evt' with 'obj' to its listeners
 * 'obj' is released (like json_object_put)
 * Returns the count of listener taht received the event.
 */
static int evt_push(struct afb_evt_event *evt, struct json_object *obj)
{
	int result;
	struct afb_evt_watch *watch;
	struct afb_evt_listener *listener;

	result = 0;
	watch = evt->watchs;
	while(watch) {
		listener = watch->listener;
		assert(listener->itf->push != NULL);
		if (watch->activity != 0)
			listener->itf->push(listener->closure, evt->name, evt->id, json_object_get(obj));
		watch = watch->next_by_event;
		result++;
	}
	json_object_put(obj);
	return result;
}

/*
 * Returns the name associated to the event 'evt'.
 */
static const char *evt_name(struct afb_evt_event *evt)
{
	return evt->name;
}

/*
 * remove the 'watch'
 */
static void remove_watch(struct afb_evt_watch *watch)
{
	struct afb_evt_watch **prv;
	struct afb_evt_event *evt;
	struct afb_evt_listener *listener;

	/* notify listener if needed */
	evt = watch->event;
	listener = watch->listener;
	if (watch->activity != 0 && listener->itf->remove != NULL)
		listener->itf->remove(listener->closure, evt->name, evt->id);

	/* unlink the watch for its event */
	prv = &evt->watchs;
	while(*prv != watch)
		prv = &(*prv)->next_by_event;
	*prv = watch->next_by_event;

	/* unlink the watch for its listener */
	prv = &listener->watchs;
	while(*prv != watch)
		prv = &(*prv)->next_by_listener;
	*prv = watch->next_by_listener;

	/* recycle memory */
	free(watch);
}

/*
 * Destroys the event 'evt'
 */
static void evt_destroy(struct afb_evt_event *evt)
{
	struct afb_evt_event **prv;
	if (evt != NULL) {
		/* removes the event if valid! */
		prv = &events;
		while (*prv != NULL) {
			if (*prv != evt)
				prv = &(*prv)->next;
			else {
				/* valid, unlink */
				*prv = evt->next;

				/* removes all watchers */
				while(evt->watchs != NULL)
					remove_watch(evt->watchs);

				/* free */
				free(evt);
				break;
			}
		}
	}
}

/*
 * Creates an event of 'name' and returns it.
 * Returns an event with closure==NULL in case of error.
 */
struct afb_event afb_evt_create_event(const char *name)
{
	size_t len;
	struct afb_evt_event *evt;

	/* allocates the id */
	do {
		if (++event_id_counter < 0) {
			event_id_wrapped = 1;
			event_id_counter = 1024; /* heuristic: small numbers are not destroyed */
		}
		if (!event_id_wrapped)
			break;
		evt = events;
		while(evt != NULL && evt->id != event_id_counter)
			evt = evt->next;
	} while (evt != NULL);

	/* allocates the event */
	len = strlen(name);
	evt = malloc(len + sizeof * evt);
	if (evt == NULL)
		goto error;

	/* initialize the event */
	evt->next = events;
	evt->watchs = NULL;
	evt->id = event_id_counter;
	assert(evt->id > 0);
	memcpy(evt->name, name, len + 1);
	events = evt;

	/* returns the event */
	return (struct afb_event){ .itf = &afb_evt_event_itf, .closure = evt };
error:
	return (struct afb_event){ .itf = NULL, .closure = NULL };
}

/*
 * Returns the name of the 'event'
 */
const char *afb_evt_event_name(struct afb_event event)
{
	return (event.itf != &afb_evt_event_itf) ? NULL : ((struct afb_evt_event *)event.closure)->name;
}

/*
 * Returns the id of the 'event'
 */
int afb_evt_event_id(struct afb_event event)
{
	return (event.itf != &afb_evt_event_itf) ? 0 : ((struct afb_evt_event *)event.closure)->id;
}

/*
 * Returns an instance of the listener defined by the 'send' callback
 * and the 'closure'.
 * Returns NULL in case of memory depletion.
 */
struct afb_evt_listener *afb_evt_listener_create(const struct afb_evt_itf *itf, void *closure)
{
	struct afb_evt_listener *listener;

	/* search if an instance already exists */
	listener = listeners;
	while (listener != NULL) {
		if (listener->itf == itf && listener->closure == closure)
			return afb_evt_listener_addref(listener);
		listener = listener->next;
	}

	/* allocates */
	listener = calloc(1, sizeof *listener);
	if (listener != NULL) {
		/* init */
		listener->next = listeners;
		listener->itf = itf;
		listener->closure = closure;
		listener->watchs = NULL;
		listener->refcount = 1;
		listeners = listener;
	}
	return listener;
}

/*
 * Increases the reference count of 'listener' and returns it
 */
struct afb_evt_listener *afb_evt_listener_addref(struct afb_evt_listener *listener)
{
	listener->refcount++;
	return listener;
}

/*
 * Decreases the reference count of the 'listener' and destroys it
 * when no more used.
 */
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

/*
 * Makes the 'listener' watching 'event'
 * Returns 0 in case of success or else -1.
 */
int afb_evt_add_watch(struct afb_evt_listener *listener, struct afb_event event)
{
	struct afb_evt_watch *watch;
	struct afb_evt_event *evt;

	/* check parameter */
	if (event.itf != &afb_evt_event_itf || listener->itf->push == NULL) {
		errno = EINVAL;
		return -1;
	}

	/* search the existing watch for the listener */
	evt = event.closure;
	watch = listener->watchs;
	while(watch != NULL) {
		if (watch->event == evt)
			goto found;
		watch = watch->next_by_listener;
	}

	/* not found, allocate a new */
	watch = malloc(sizeof *watch);
	if (watch == NULL) {
		errno = ENOMEM;
		return -1;
	}

	/* initialise and link */
	watch->event = evt;
	watch->next_by_event = evt->watchs;
	watch->listener = listener;
	watch->next_by_listener = listener->watchs;
	watch->activity = 0;
	evt->watchs = watch;
	listener->watchs = watch;

found:
	if (watch->activity == 0 && listener->itf->add != NULL)
		listener->itf->add(listener->closure, evt->name, evt->id);
	watch->activity++;

	return 0;
}

/*
 * Avoids the 'listener' to watch 'event'
 * Returns 0 in case of success or else -1.
 */
int afb_evt_remove_watch(struct afb_evt_listener *listener, struct afb_event event)
{
	struct afb_evt_watch *watch;
	struct afb_evt_event *evt;

	/* check parameter */
	if (event.itf != &afb_evt_event_itf) {
		errno = EINVAL;
		return -1;
	}

	/* search the existing watch */
	evt = event.closure;
	watch = listener->watchs;
	while(watch != NULL) {
		if (watch->event == evt) {
			/* found: remove it */
			if (watch->activity != 0) {
				watch->activity--;
				if (watch->activity == 0 && listener->itf->remove != NULL)
					listener->itf->remove(listener->closure, evt->name, evt->id);
			}
			return 0;
		}
		watch = watch->next_by_listener;
	}
	errno = ENOENT;
	return -1;
}

