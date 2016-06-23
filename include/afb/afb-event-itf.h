/*
 * Copyright (C) 2016 "IoT.bzh"
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

#pragma once

/* avoid inclusion of <json-c/json.h> */
struct json_object;

/*
 * Interface for handling requests.
 * It records the functions to be called for the request.
 * Don't use this structure directly.
 * Use the helper functions documented below.
 */ 
struct afb_event_itf {
	/* CAUTION: respect the order, add at the end */

	int (*broadcast)(void *closure, struct json_object *obj);
	int (*push)(void *closure, struct json_object *obj);
	void (*drop)(void *closure);
};

/*
 * Describes the request of afb-daemon for bindings
 */
struct afb_event {
	const struct afb_event_itf *itf;	/* the interface to use */
	void *closure;				/* the closure argument for functions of 'itf' */
};

/*
 * Broadcasts widely the 'event' with the data 'object'.
 * 'object' can be NULL.
 *
 * For conveniency, the function calls 'json_object_put' for 'object'.
 * Thus, in the case where 'object' should remain available after
 * the function returns, the function 'json_object_get' shall be used.
 *
 * Returns the count of clients that received the event.
 */
static inline int afb_event_broadcast(struct afb_event event, struct json_object *object)
{
	return event.itf->broadcast(event.closure, object);
}

/*
 * Pushes the 'event' with the data 'object' to its obeservers.
 * 'object' can be NULL.
 *
 * For conveniency, the function calls 'json_object_put' for 'object'.
 * Thus, in the case where 'object' should remain available after
 * the function returns, the function 'json_object_get' shall be used.
 *
 * Returns the count of clients that received the event.
 */
static inline int afb_event_push(struct afb_event event, struct json_object *object)
{
	return event.itf->push(event.closure, object);
}

/*
 * Drops the data associated to the event
 * After calling this function, the event
 * MUST NOT BE USED ANYMORE.
 */
static inline void afb_event_drop(struct afb_event event)
{
	event.itf->drop(event.closure);
}

