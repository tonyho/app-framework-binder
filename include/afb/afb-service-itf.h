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
 * Interface for internal of services
 * It records the functions to be called for the request.
 * Don't use this structure directly.
 * Use the helper functions documented below.
 */
struct afb_service_itf
{
	/* CAUTION: respect the order, add at the end */

	void (*call)(void *closure, const char *api, const char *verb, struct json_object *args, void (*callback)(void*, int, struct json_object*), void *callback_closure);
};

/*
 * Object that encapsulate accesses to service items
 */
struct afb_service
{
	const struct afb_service_itf *itf;
	void *closure;
};

/*
 * When a binding have an exported implementation of the
 * function 'afbBindingV1ServiceInit', defined below,
 * the framework calls it for initialising the service after
 * registration of all bindings.
 *
 * The object 'service' should be recorded. It has functions that
 * allows the binding to call features with its own personality.
 *
 * The function should return 0 in case of success or, else, should return
 * a negative value.
 */
extern int afbBindingV1ServiceInit(struct afb_service service);

/*
 * When a binding have an implementation of the function 'afbBindingV1ServiceEvent',
 * defined below, the framework calls that function for any broadcasted event or for
 * events that the service subscribed to in its name.
 *
 * It receive the 'event' name and its related data in 'object' (be aware that 'object'
 * might be NULL).
 */
extern void afbBindingV1ServiceEvent(const char *event, struct json_object *object);

/*
 * Calls the 'verb' of the 'api' with the arguments 'args' and 'verb' in the name of the binding.
 * The result of the call is delivered to the 'callback' function with the 'callback_closure'.
 *
 * The 'callcack' receives 3 arguments:
 *  1. 'closure' the user defined closure pointer 'callback_closure',
 *  2. 'iserror' a boolean status being true (not null) when an error occured,
 *  2. 'result' the resulting data as a JSON object.
 *
 * See also 'afb_req_subcall'
 *
 * Returns 0in case of success or -1 in case of error.
 */
static inline void afb_service_call(struct afb_service service, const char *api, const char *verb, struct json_object *args, void (*callback)(void*closure, int iserror, struct json_object *result), void *callback_closure)
{
	service.itf->call(service.closure, api, verb, args, callback, callback_closure);
}

