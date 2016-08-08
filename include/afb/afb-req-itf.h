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

#if !defined(_GNU_SOURCE)
# error "_GNU_SOURCE must be defined for using vasprintf"
#endif

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

#include <afb/afb-event-itf.h>

/* avoid inclusion of <json-c/json.h> */
struct json_object;

/*
 * Describes an argument (or parameter) of a request
 */
struct afb_arg {
	const char *name;	/* name of the argument or NULL if invalid */
	const char *value;	/* string representation of the value of the argument */
				/* original filename of the argument if path != NULL */
	const char *path;	/* if not NULL, path of the received file for the argument */
				/* when the request is finalized this file is removed */
};

/*
 * Interface for handling requests.
 * It records the functions to be called for the request.
 * Don't use this structure directly.
 * Use the helper functions documented below.
 */
struct afb_req_itf {
	/* CAUTION: respect the order, add at the end */

	struct json_object *(*json)(void *closure);
	struct afb_arg (*get)(void *closure, const char *name);

	void (*success)(void *closure, struct json_object *obj, const char *info);
	void (*fail)(void *closure, const char *status, const char *info);

	const char *(*raw)(void *closure, size_t *size);
	void (*send)(void *closure, const char *buffer, size_t size);

	void *(*context_get)(void *closure);
	void (*context_set)(void *closure, void *value, void (*free_value)(void*));

	void (*addref)(void *closure);
	void (*unref)(void *closure);

	void (*session_close)(void *closure);
	int (*session_set_LOA)(void *closure, unsigned level);

	int (*subscribe)(void *closure, struct afb_event event);
	int (*unsubscribe)(void *closure, struct afb_event event);

	void (*subcall)(void *closure, const char *api, const char *verb, struct json_object *args, void (*callback)(void*, int, struct json_object*), void *cb_closure);
};

/*
 * Describes the request by bindings from afb-daemon
 */
struct afb_req {
	const struct afb_req_itf *itf;	/* the interface to use */
	void *closure;			/* the closure argument for functions of 'itf' */
};

/*
 * Checks wether the request 'req' is valid or not.
 *
 * Returns 0 if not valid or 1 if valid.
 */
static inline int afb_req_is_valid(struct afb_req req)
{
	return req.itf != NULL;
}

/*
 * Gets from the request 'req' the argument of 'name'.
 * Returns a PLAIN structure of type 'struct afb_arg'.
 * When the argument of 'name' is not found, all fields of result are set to NULL.
 * When the argument of 'name' is found, the fields are filled,
 * in particular, the field 'result.name' is set to 'name'.
 *
 * There is a special name value: the empty string.
 * The argument of name "" is defined only if the request was made using
 * an HTTP POST of Content-Type "application/json". In that case, the
 * argument of name "" receives the value of the body of the HTTP request.
 */
static inline struct afb_arg afb_req_get(struct afb_req req, const char *name)
{
	return req.itf->get(req.closure, name);
}

/*
 * Gets from the request 'req' the string value of the argument of 'name'.
 * Returns NULL if when there is no argument of 'name'.
 * Returns the value of the argument of 'name' otherwise.
 *
 * Shortcut for: afb_req_get(req, name).value
 */
static inline const char *afb_req_value(struct afb_req req, const char *name)
{
	return afb_req_get(req, name).value;
}

/*
 * Gets from the request 'req' the path for file attached to the argument of 'name'.
 * Returns NULL if when there is no argument of 'name' or when there is no file.
 * Returns the path of the argument of 'name' otherwise.
 *
 * Shortcut for: afb_req_get(req, name).path
 */
static inline const char *afb_req_path(struct afb_req req, const char *name)
{
	return afb_req_get(req, name).path;
}

/*
 * Gets from the request 'req' the json object hashing the arguments.
 * The returned object must not be released using 'json_object_put'.
 */
static inline struct json_object *afb_req_json(struct afb_req req)
{
	return req.itf->json(req.closure);
}

/*
 * Sends a reply of kind success to the request 'req'.
 * The status of the reply is automatically set to "success".
 * Its send the object 'obj' (can be NULL) with an
 * informationnal comment 'info (can also be NULL).
 *
 * For convenience, the function calls 'json_object_put' for 'obj'.
 * Thus, in the case where 'obj' should remain available after
 * the function returns, the function 'json_object_get' shall be used.
 */
static inline void afb_req_success(struct afb_req req, struct json_object *obj, const char *info)
{
	req.itf->success(req.closure, obj, info);
}

/*
 * Same as 'afb_req_success' but the 'info' is a formatting
 * string followed by arguments.
 *
 * For convenience, the function calls 'json_object_put' for 'obj'.
 * Thus, in the case where 'obj' should remain available after
 * the function returns, the function 'json_object_get' shall be used.
 */
static inline void afb_req_success_f(struct afb_req req, struct json_object *obj, const char *info, ...)
{
	char *message;
	va_list args;
	va_start(args, info);
	if (info == NULL || vasprintf(&message, info, args) < 0)
		message = NULL;
	va_end(args);
	afb_req_success(req, obj, message);
	free(message);
}

/*
 * Sends a reply of kind failure to the request 'req'.
 * The status of the reply is set to 'status' and an
 * informationnal comment 'info' (can also be NULL) can be added.
 *
 * Note that calling afb_req_fail("success", info) is equivalent
 * to call afb_req_success(NULL, info). Thus even if possible it
 * is strongly recommanded to NEVER use "success" for status.
 *
 * For convenience, the function calls 'json_object_put' for 'obj'.
 * Thus, in the case where 'obj' should remain available after
 * the function returns, the function 'json_object_get' shall be used.
 */
static inline void afb_req_fail(struct afb_req req, const char *status, const char *info)
{
	req.itf->fail(req.closure, status, info);
}

/*
 * Same as 'afb_req_fail' but the 'info' is a formatting
 * string followed by arguments.
 *
 * For convenience, the function calls 'json_object_put' for 'obj'.
 * Thus, in the case where 'obj' should remain available after
 * the function returns, the function 'json_object_get' shall be used.
 */
static inline void afb_req_fail_f(struct afb_req req, const char *status, const char *info, ...)
{
	char *message;
	va_list args;
	va_start(args, info);
	if (info == NULL || vasprintf(&message, info, args) < 0)
		message = NULL;
	va_end(args);
	afb_req_fail(req, status, message);
	free(message);
}

/*
 * Gets the pointer stored by the binding for the session of 'req'.
 * When the binding has not yet recorded a pointer, NULL is returned.
 */
static inline void *afb_req_context_get(struct afb_req req)
{
	return req.itf->context_get(req.closure);
}

/*
 * Stores for the binding the pointer 'context' to the session of 'req'.
 * The function 'free_context' will be called when the session is closed
 * or if binding stores an other pointer.
 */
static inline void afb_req_context_set(struct afb_req req, void *context, void (*free_context)(void*))
{
	req.itf->context_set(req.closure, context, free_context);
}

/*
 * Gets the pointer stored by the binding for the session of 'req'.
 * If the stored pointer is NULL, indicating that no pointer was
 * already stored, afb_req_context creates a new context by calling
 * the function 'create_context' and stores it with the freeing function
 * 'free_context'.
 */
static inline void *afb_req_context(struct afb_req req, void *(*create_context)(), void (*free_context)(void*))
{
	void *result = afb_req_context_get(req);
	if (result == NULL) {
		result = create_context();
		afb_req_context_set(req, result, free_context);
	}
	return result;
}

/*
 * Frees the pointer stored by the binding for the session of 'req'
 * and sets it to NULL.
 *
 * Shortcut for: afb_req_context_set(req, NULL, NULL)
 */
static inline void afb_req_context_clear(struct afb_req req)
{
	afb_req_context_set(req, NULL, NULL);
}

/*
 * Adds one to the count of references of 'req'.
 * This function MUST be called by asynchronous implementations
 * of verbs if no reply was sent before returning.
 */
static inline void afb_req_addref(struct afb_req req)
{
	req.itf->addref(req.closure);
}

/*
 * Substracts one to the count of references of 'req'.
 * This function MUST be called by asynchronous implementations
 * of verbs after sending the asynchronous reply.
 */
static inline void afb_req_unref(struct afb_req req)
{
	req.itf->unref(req.closure);
}

/*
 * Closes the session associated with 'req'
 * and delete all associated contexts.
 */
static inline void afb_req_session_close(struct afb_req req)
{
	req.itf->session_close(req.closure);
}

/*
 * Sets the level of assurance of the session of 'req'
 * to 'level'. The effect of this function is subject of
 * security policies.
 * Returns 1 on success or 0 if failed.
 */
static inline int afb_req_session_set_LOA(struct afb_req req, unsigned level)
{
	return req.itf->session_set_LOA(req.closure, level);
}

/*
 * Stores 'req' on heap for asynchrnous use.
 * Returns a pointer to the stored 'req' or NULL on memory depletion.
 * The count of reference to 'req' is incremented on success
 * (see afb_req_addref).
 */
static inline struct afb_req *afb_req_store(struct afb_req req)
{
	struct afb_req *result = malloc(sizeof *result);
	if (result != NULL) {
		*result = req;
		afb_req_addref(req);
	}
	return result;
}

/*
 * Retrieves the afb_req stored at 'req' and frees the memory.
 * Returns the stored request.
 * The count of reference is UNCHANGED, thus, normally, the
 * function 'afb_req_unref' should be called on the result
 * after that the asynchronous reply if sent.
 */
static inline struct afb_req afb_req_unstore(struct afb_req *req)
{
	struct afb_req result = *req;
	free(req);
	return result;
}

/*
 * Establishes for the client link identified by 'req' a subscription
 * to the 'event'.
 * Returns 0 in case of successful subscription or -1 in case of error.
 */
static inline int afb_req_subscribe(struct afb_req req, struct afb_event event)
{
	return req.itf->subscribe(req.closure, event);
}

/*
 * Revokes the subscription established to the 'event' for the client
 * link identified by 'req'.
 * Returns 0 in case of successful subscription or -1 in case of error.
 */
static inline int afb_req_unsubscribe(struct afb_req req, struct afb_event event)
{
	return req.itf->unsubscribe(req.closure, event);
}

/*
 * Makes a call to the method of name 'api' / 'verb' with the object 'args'.
 * This call is made in the context of the request 'req'.
 * On completion, the function 'callback' is invoked with the
 * 'closure' given at call and two other parameters: 'iserror' and 'result'.
 * 'iserror' is a boolean that indicates if the reply is an error reply.
 * 'result' is the json object of the reply.
 */
static inline void afb_req_subcall(struct afb_req req, const char *api, const char *verb, struct json_object *args, void (*callback)(void *closure, int iserror, struct json_object *result), void *closure)
{
	req.itf->subcall(req.closure, api, verb, args, callback, closure);
}

/* internal use */
static inline const char *afb_req_raw(struct afb_req req, size_t *size)
{
	return req.itf->raw(req.closure, size);
}

/* internal use */
static inline void afb_req_send(struct afb_req req, const char *buffer, size_t size)
{
	req.itf->send(req.closure, buffer, size);
}

