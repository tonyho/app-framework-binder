/* 
 * Copyright (C) 2015, 2016 "IoT.bzh"
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
#define NO_PLUGIN_VERBOSE_MACRO

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <systemd/sd-bus.h>
#include <json-c/json.h>

#include <afb/afb-plugin.h>
#include <afb/afb-req-itf.h>

#include "afb-common.h"

#include "session.h"
#include "afb-apis.h"
#include "afb-api-so.h"
#include "afb-context.h"
#include "verbose.h"

static const char DEFAULT_PATH_PREFIX[] = "/org/agl/afb/api/";

/*
 * The path given are of the form
 *     system:/org/agl/afb/api/...
 * or
 *     user:/org/agl/afb/api/...
 */
struct api_dbus
{
	struct sd_bus *sdbus;	/* the bus */
	struct sd_bus_slot *slot; /* the slot */
	char *path;		/* path of the object for the API */
	char *name;		/* name/interface of the object */
	char *api;		/* api name of the interface */
};

#define RETOK   1
#define RETERR  2
#define RETRAW  3

/******************* common part **********************************/

/*
 * create a structure api_dbus connected on either the system
 * bus if 'system' is not null or on the user bus. The connection
 * is established for either emiting/receiving on 'path' being of length
 * 'pathlen'.
 */
static struct api_dbus *make_api_dbus_3(int system, const char *path, size_t pathlen)
{
	struct api_dbus *api;
	struct sd_bus *sdbus;
	char *ptr;

	/* allocates the structure */
	api = calloc(1, sizeof *api + 1 + pathlen + pathlen);
	if (api == NULL) {
		errno = ENOMEM;
		goto error;
	}

	/* init the structure's strings */

	/* path is copied after the struct */
	api->path = (void*)(api+1);
	strcpy(api->path, path);

	/* api name is at the end of the path */
	api->api = strrchr(api->path, '/');
	if (api->api == NULL) {
		errno = EINVAL;
		goto error2;
	}
	api->api++;
	if (!afb_apis_is_valid_api_name(api->api)) {
		errno = EINVAL;
		goto error2;
	}

	/* the name/interface is copied after the path */
	api->name = &api->path[pathlen + 1];
	strcpy(api->name, &path[1]);
	ptr = strchr(api->name, '/');
	while(ptr != NULL) {
		*ptr = '.';
		ptr = strchr(ptr, '/');
	}

	/* choose the bus */
	sdbus = (system ? afb_common_get_system_bus : afb_common_get_user_bus)();
	if (sdbus == NULL)
		goto error2;

	api->sdbus = sdbus;
	return api;

error2:
	free(api);
error:
	return NULL;
}

/*
 * create a structure api_dbus connected on either the system
 * bus if 'system' is not null or on the user bus. The connection
 * is established for either emiting/receiving on 'path'.
 * If 'path' is not absolute, it is prefixed with DEFAULT_PATH_PREFIX.
 */
static struct api_dbus *make_api_dbus_2(int system, const char *path)
{
	size_t len;
	char *ptr;

	/* check the length of the path */
	len = strlen(path);
	if (len == 0) {
		errno = EINVAL;
		return NULL;
	}

	/* if the path is absolute, creation now */
	if (path[0] == '/')
		return make_api_dbus_3(system, path, len);

	/* compute the path prefixed with DEFAULT_PATH_PREFIX */
	assert(strlen(DEFAULT_PATH_PREFIX) > 0);
	assert(DEFAULT_PATH_PREFIX[strlen(DEFAULT_PATH_PREFIX) - 1] == '/');
	len += strlen(DEFAULT_PATH_PREFIX);
	ptr = alloca(len + 1);
	strcpy(stpcpy(ptr, DEFAULT_PATH_PREFIX), path);

	/* creation for prefixed path */
	return make_api_dbus_3(system, ptr, len);
}

/*
 * create a structure api_dbus connected either emiting/receiving
 * on 'path'.
 * The path can be prefixed with "system:" or "user:" to select
 * either the user or the system D-Bus. If none is set then user's
 * bus is selected.
 * If remaining 'path' is not absolute, it is prefixed with
 * DEFAULT_PATH_PREFIX.
 */
static struct api_dbus *make_api_dbus(const char *path)
{
	const char *ptr;
	size_t preflen;

	/* retrieves the prefix "scheme-like" part */
	ptr = strchr(path, ':');
	if (ptr == NULL)
		return make_api_dbus_2(0, path);

	/* check the prefix part */
	preflen = (size_t)(ptr - path);
	if (strncmp(path, "system", preflen) == 0)
		return make_api_dbus_2(1, ptr + 1);

	if (strncmp(path, "user", preflen) == 0)
		return make_api_dbus_2(0, ptr + 1);

	/* TODO: connect to a foreign D-Bus? */
	errno = EINVAL;
	return NULL;
}

static void destroy_api_dbus(struct api_dbus *api)
{
	free(api);
}

/******************* client part **********************************/

/*
 * structure for recording query data
 */
struct dbus_memo {
	struct afb_req req;		/* the request handle */
	struct afb_context *context;	/* the context of the query */
};

/* allocates and init the memorizing data */
static struct dbus_memo *api_dbus_client_make_memo(struct afb_req req, struct afb_context *context)
{
	struct dbus_memo *memo;

	memo = malloc(sizeof *memo);
	if (memo != NULL) {
		afb_req_addref(req);
		memo->req = req;
		memo->context = context;
	}
	return memo;
}

/* free and release the memorizing data */
static void api_dbus_client_free_memo(struct dbus_memo *memo)
{
	afb_req_unref(memo->req);
	free(memo);
}

/* callback when received answer */
static int api_dbus_client_on_reply(sd_bus_message *message, void *userdata, sd_bus_error *ret_error)
{
	int rc;
	struct dbus_memo *memo;
	const char *first, *second;
	uint8_t type;
	uint32_t flags;

	/* retrieve the recorded data */
	memo = userdata;

	/* get the answer */
	rc = sd_bus_message_read(message, "yssu", &type, &first, &second, &flags);
	if (rc < 0) {
		/* failing to have the answer */
		afb_req_fail(memo->req, "error", "dbus error");
	} else {
		/* report the answer */
		memo->context->flags = (unsigned)flags;
		switch(type) {
		case RETOK:
			afb_req_success(memo->req, json_tokener_parse(first), second);
			break;
		case RETERR:
			afb_req_fail(memo->req, first, second);
			break;
		case RETRAW:
			afb_req_send(memo->req, first, strlen(first));
			break;
		default:
			afb_req_fail(memo->req, "error", "dbus link broken");
			break;
		}
	}
	api_dbus_client_free_memo(memo);
	return 1;
}

/* on call, propagate it to the dbus service */
static void api_dbus_client_call(struct api_dbus *api, struct afb_req req, struct afb_context *context, const char *verb, size_t lenverb)
{
	size_t size;
	int rc;
	char *method = strndupa(verb, lenverb);
	struct dbus_memo *memo;

	/* create the recording data */
	memo = api_dbus_client_make_memo(req, context);
	if (memo == NULL) {
		afb_req_fail(req, "error", "out of memory");
		return;
	}

	/* makes the call */
	rc = sd_bus_call_method_async(api->sdbus, NULL,
		api->name, api->path, api->name, method,
		api_dbus_client_on_reply, memo,
		"ssu",
			afb_req_raw(req, &size),
			ctxClientGetUuid(context->session),
			(uint32_t)context->flags);

	/* if there was an error report it directly */
	if (rc < 0) {
		errno = -rc;
		afb_req_fail(req, "error", "dbus error");
		api_dbus_client_free_memo(memo);
	}
}

/* receives events */
static int api_dbus_client_on_event(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
	struct json_object *object;
	const char *event, *data;
	int rc = sd_bus_message_read(m, "ss", &event, &data);
	if (rc < 0)
		ERROR("unreadable event");
	else {
		object = json_tokener_parse(data);
		ctxClientEventSend(NULL, event, object);
		json_object_put(object);
	}
	return 1;
}

/* adds a afb-dbus-service client api */
int afb_api_dbus_add_client(const char *path)
{
	int rc;
	struct api_dbus *api;
	struct afb_api afb_api;
	char *match;

	/* create the dbus client api */
	api = make_api_dbus(path);
	if (api == NULL)
		goto error;

	/* connect to events */
	rc = asprintf(&match, "type='signal',path='%s',interface='%s',member='event'", api->path, api->name);
	if (rc < 0) {
		errno = ENOMEM;
		ERROR("out of memory");
		goto error;
	}
	rc = sd_bus_add_match(api->sdbus, &api->slot, match, api_dbus_client_on_event, api);
	free(match);
	if (rc < 0) {
		errno = -rc;
		ERROR("can't add dbus object %s for %s", api->path, api->name);
		goto error;
	}

	/* record it as an API */
	afb_api.closure = api;
	afb_api.call = (void*)api_dbus_client_call;
	if (afb_apis_add(api->api, afb_api) < 0)
		goto error2;

	return 0;

error2:
	destroy_api_dbus(api);
error:
	return -1;
}

/******************* dbus request part for server *****************/

/*
 * structure for a dbus request
 */
struct dbus_req {
	struct afb_context context;	/* the context, should be THE FIRST */
	sd_bus_message *message;	/* the incoming request message */
	const char *request;		/* the readen request as string */
	struct json_object *json;	/* the readen request as object */
	int refcount;			/* reference count of the request */
};

/* increment the reference count of the request */
static void dbus_req_addref(struct dbus_req *dreq)
{
	dreq->refcount++;
}

/* decrement the reference count of the request and free/release it on falling to null */
static void dbus_req_unref(struct dbus_req *dreq)
{
	if (dreq == NULL || --dreq->refcount)
		return;

	afb_context_disconnect(&dreq->context);
	json_object_put(dreq->json);
	sd_bus_message_unref(dreq->message);
	free(dreq);
}

/* get the object of the request */
static struct json_object *dbus_req_json(struct dbus_req *dreq)
{
	if (dreq->json == NULL) {
		dreq->json = json_tokener_parse(dreq->request);
		if (dreq->json == NULL) {
			/* lazy error detection of json request. Is it to improve? */
			dreq->json = json_object_new_string(dreq->request);
		}
	}
	return dreq->json;
}

/* get the argument of the request of 'name' */
static struct afb_arg dbus_req_get(struct dbus_req *dreq, const char *name)
{
	struct afb_arg arg;
	struct json_object *value, *root;

	root = dbus_req_json(dreq);
	if (root != NULL && json_object_object_get_ex(root, name, &value)) {
		arg.name = name;
		arg.value = json_object_get_string(value);
	} else {
		arg.name = NULL;
		arg.value = NULL;
	}
	arg.path = NULL;
	return arg;
}

static void dbus_req_reply(struct dbus_req *dreq, uint8_t type, const char *first, const char *second)
{
	int rc;
	rc = sd_bus_reply_method_return(dreq->message,
			"yssu", type, first, second, (uint32_t)dreq->context.flags);
	if (rc < 0)
		ERROR("sending the reply failed");
}

static void dbus_req_success(struct dbus_req *dreq, struct json_object *obj, const char *info)
{
	dbus_req_reply(dreq, RETOK, json_object_to_json_string_ext(obj, JSON_C_TO_STRING_PLAIN), info);
}

static void dbus_req_fail(struct dbus_req *dreq, const char *status, const char *info)
{
	dbus_req_reply(dreq, RETERR, status, info);
}

static const char *dbus_req_raw(struct dbus_req *dreq, size_t *size)
{
	if (size != NULL)
		*size = strlen(dreq->request);
	return dreq->request;
}

static void dbus_req_send(struct dbus_req *dreq, const char *buffer, size_t size)
{
	/* TODO: how to put sized buffer as strings? things aren't clear here!!! */
	dbus_req_reply(dreq, RETRAW, buffer, "");
}

struct afb_req_itf dbus_req_itf = {
	.json = (void*)dbus_req_json,
	.get = (void*)dbus_req_get,
	.success = (void*)dbus_req_success,
	.fail = (void*)dbus_req_fail,
	.raw = (void*)dbus_req_raw,
	.send = (void*)dbus_req_send,
	.context_get = (void*)afb_context_get,
	.context_set = (void*)afb_context_set,
	.addref = (void*)dbus_req_addref,
	.unref = (void*)dbus_req_unref
};

/******************* server part **********************************/

/* called when the object for the service is called */
static int api_dbus_server_on_object_called(sd_bus_message *message, void *userdata, sd_bus_error *ret_error)
{
	int rc;
	const char *method;
	const char *uuid;
	struct dbus_req *dreq;
	struct api_dbus *api = userdata;
	struct afb_req areq;
	uint32_t flags;

	/* check the interface */
	if (strcmp(sd_bus_message_get_interface(message), api->name) != 0)
		return 0;

	/* get the method */
	method = sd_bus_message_get_member(message);

	/* create the request */
	dreq = calloc(1 , sizeof *dreq);
	if (dreq == NULL) {
		sd_bus_reply_method_errorf(message, SD_BUS_ERROR_NO_MEMORY, "out of memory");
		return 1;
	}

	/* get the data */
	rc = sd_bus_message_read(message, "ssu", &dreq->request, &uuid, &flags);
	if (rc < 0) {
		sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_SIGNATURE, "invalid signature");
		free(dreq);
		return 1;
	}

	/* connect to the context */
	if (afb_context_connect(&dreq->context, uuid, NULL) < 0) {
		sd_bus_reply_method_errorf(message, SD_BUS_ERROR_NO_MEMORY, "out of memory");
		free(dreq);
		return 1;
	}

	/* fulfill the request and emit it */
	dreq->context.flags = flags;
	dreq->message = sd_bus_message_ref(message);
	dreq->json = NULL;
	dreq->refcount = 1;
	areq.itf = &dbus_req_itf;
	areq.closure = dreq;
	afb_apis_call_(areq, &dreq->context, api->api, method);
	dbus_req_unref(dreq);
	return 1;
}

static void afb_api_dbus_server_send_event(struct api_dbus *api, const char *event, struct json_object *object)
{
	int rc;

	rc = sd_bus_emit_signal(api->sdbus, api->path, api->name,
			"event", "ss", event, json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN));
	if (rc < 0)
		ERROR("error while emiting event %s", event);
	json_object_put(object);
}

static int afb_api_dbus_server_expects_event(struct api_dbus *api, const char *event)
{
	size_t len = strlen(api->api);
	if (strncasecmp(event, api->api, len) != 0)
		return 0;
	return event[len] == '.';
}

static struct afb_event_listener_itf evitf = {
	.send = (void*)afb_api_dbus_server_send_event,
	.expects = (void*)afb_api_dbus_server_expects_event
};

/* create the service */
int afb_api_dbus_add_server(const char *path)
{
	int rc;
	struct api_dbus *api;

	/* get the dbus api object connected */
	api = make_api_dbus(path);
	if (api == NULL)
		goto error;

	/* request the service object name */
	rc = sd_bus_request_name(api->sdbus, api->name, 0);
	if (rc < 0) {
		errno = -rc;
		ERROR("can't register name %s", api->name);
		goto error2;
	}

	/* connect the service to the dbus object */
	rc = sd_bus_add_object(api->sdbus, &api->slot, api->path, api_dbus_server_on_object_called, api);
	if (rc < 0) {
		errno = -rc;
		ERROR("can't add dbus object %s for %s", api->path, api->name);
		goto error3;
	}
	INFO("afb service over dbus installed, name %s, path %s", api->name, api->path);

	ctxClientEventListenerAdd(NULL, (struct afb_event_listener){ .itf = &evitf, .closure = api });

	return 0;
error3:
	sd_bus_release_name(api->sdbus, api->name);
error2:
	destroy_api_dbus(api);
error:
	return -1;
}


