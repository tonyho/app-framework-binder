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

#include <afb/afb-req-itf.h>

#include "afb-common.h"

#include "session.h"
#include "afb-msg-json.h"
#include "afb-apis.h"
#include "afb-api-so.h"
#include "afb-context.h"
#include "afb-evt.h"
#include "afb-subcall.h"
#include "verbose.h"

static const char DEFAULT_PATH_PREFIX[] = "/org/agl/afb/api/";

struct dbus_memo;
struct dbus_event;
struct destination;

/*
 * The path given are of the form
 *     system:/org/agl/afb/api/...
 * or
 *     user:/org/agl/afb/api/...
 */
struct api_dbus
{
	struct sd_bus *sdbus;	/* the bus */
	char *path;		/* path of the object for the API */
	char *name;		/* name/interface of the object */
	char *api;		/* api name of the interface */
	union {
		struct {
			struct sd_bus_slot *slot_broadcast;
			struct sd_bus_slot *slot_event;
			struct dbus_event *events;
			struct dbus_memo *memos;
		} client;
		struct {
			struct sd_bus_slot *slot_call;
			struct afb_evt_listener *listener; /* listener for broadcasted events */
			struct destination *destinations;
		} server;
	};
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
	struct dbus_memo *next;		/* the next memo */
	struct api_dbus *api;		/* the dbus api */
	struct afb_req req;		/* the request handle */
	struct afb_context *context;	/* the context of the query */
	uint64_t msgid;			/* the message identifier */
};

struct dbus_event
{
	struct dbus_event *next;
	struct afb_event event;
	int id;
	int refcount;
};

/* allocates and init the memorizing data */
static struct dbus_memo *api_dbus_client_memo_make(struct api_dbus *api, struct afb_req req, struct afb_context *context)
{
	struct dbus_memo *memo;

	memo = malloc(sizeof *memo);
	if (memo != NULL) {
		afb_req_addref(req);
		memo->req = req;
		memo->context = context;
		memo->msgid = 0;
		memo->api = api;
		memo->next = api->client.memos;
		api->client.memos = memo;
	}
	return memo;
}

/* free and release the memorizing data */
static void api_dbus_client_memo_destroy(struct dbus_memo *memo)
{
	struct dbus_memo **prv;

	prv = &memo->api->client.memos;
	while (*prv != NULL) {
		if (*prv == memo) {
			*prv = memo->next;
			break;
		}
		prv = &(*prv)->next;
	}

	afb_req_unref(memo->req);
	free(memo);
}

/* search a memorized request */
static struct dbus_memo *api_dbus_client_memo_search(struct api_dbus *api, uint64_t msgid)
{
	struct dbus_memo *memo;

	memo = api->client.memos;
	while (memo != NULL && memo->msgid != msgid)
		memo = memo->next;

	return memo;
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
			afb_req_success(memo->req, json_tokener_parse(first), *second ? second : NULL);
			break;
		case RETERR:
			afb_req_fail(memo->req, first, *second ? second : NULL);
			break;
		case RETRAW:
			afb_req_send(memo->req, first, strlen(first));
			break;
		default:
			afb_req_fail(memo->req, "error", "dbus link broken");
			break;
		}
	}
	api_dbus_client_memo_destroy(memo);
	return 1;
}

/* on call, propagate it to the dbus service */
static void api_dbus_client_call(struct api_dbus *api, struct afb_req req, struct afb_context *context, const char *verb, size_t lenverb)
{
	size_t size;
	int rc;
	char *method = strndupa(verb, lenverb);
	struct dbus_memo *memo;
	struct sd_bus_message *msg;

	/* create the recording data */
	memo = api_dbus_client_memo_make(api, req, context);
	if (memo == NULL) {
		afb_req_fail(req, "error", "out of memory");
		return;
	}

	/* creates the message */
	msg = NULL;
	rc = sd_bus_message_new_method_call(api->sdbus, &msg, api->name, api->path, api->name, method);
	if (rc < 0)
		goto error;

	rc = sd_bus_message_append(msg, "ssu",
			afb_req_raw(req, &size),
			ctxClientGetUuid(context->session),
			(uint32_t)context->flags);
	if (rc < 0)
		goto error;

	/* makes the call */
	rc = sd_bus_call_async(api->sdbus, NULL, msg, api_dbus_client_on_reply, memo, (uint64_t)-1);
	if (rc < 0)
		goto error;

	rc = sd_bus_message_get_cookie(msg, &memo->msgid);
	if (rc >= 0)
		goto end;

error:
	/* if there was an error report it directly */
	errno = -rc;
	afb_req_fail(req, "error", "dbus error");
	api_dbus_client_memo_destroy(memo);
end:
	sd_bus_message_unref(msg);
}

static int api_dbus_service_start(struct api_dbus *api, int share_session, int onneed)
{
	/* not an error when onneed */
	if (onneed != 0)
		return 0;

	/* already started: it is an error */
	ERROR("The Dbus binding %s is not a startable service", api->name);
	return -1;
}

/* receives broadcasted events */
static int api_dbus_client_on_broadcast_event(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
	struct json_object *object;
	const char *event, *data;
	int rc = sd_bus_message_read(m, "ss", &event, &data);
	if (rc < 0)
		ERROR("unreadable broadcasted event");
	else {
		object = json_tokener_parse(data);
		afb_evt_broadcast(event, object);
	}
	return 1;
}

/* search the event */
static struct dbus_event *api_dbus_client_event_search(struct api_dbus *api, int id, const char *name)
{
	struct dbus_event *ev;

	ev = api->client.events;
	while (ev != NULL && (ev->id != id || 0 != strcmp(afb_evt_event_name(ev->event), name)))
		ev = ev->next;

	return ev;
}

/* adds an event */
static void api_dbus_client_event_create(struct api_dbus *api, int id, const char *name)
{
	struct dbus_event *ev;

	/* check conflicts */
	ev = api_dbus_client_event_search(api, id, name);
	if (ev != NULL) {
		ev->refcount++;
		return;
	}

	/* no conflict, try to add it */
	ev = malloc(sizeof *ev);
	if (ev != NULL) {
		ev->event = afb_evt_create_event(name);
		if (ev->event.closure == NULL)
			free(ev);
		else {
			ev->refcount = 1;
			ev->id = id;
			ev->next = api->client.events;
			api->client.events = ev;
			return;
		}
	}
	ERROR("can't create event %s, out of memory", name);
}

/* removes an event */
static void api_dbus_client_event_drop(struct api_dbus *api, int id, const char *name)
{
	struct dbus_event *ev, **prv;

	/* retrieves the event */
	ev = api_dbus_client_event_search(api, id, name);
	if (ev == NULL) {
		ERROR("event %s not found", name);
		return;
	}

	/* decrease the reference count */
	if (--ev->refcount)
		return;

	/* unlinks the event */
	prv = &api->client.events;
	while (*prv != ev)
		prv = &(*prv)->next;
	*prv = ev->next;

	/* destroys the event */
	afb_event_drop(ev->event);
	free(ev);
}

/* pushs an event */
static void api_dbus_client_event_push(struct api_dbus *api, int id, const char *name, const char *data)
{
	struct json_object *object;
	struct dbus_event *ev;

	/* retrieves the event */
	ev = api_dbus_client_event_search(api, id, name);
	if (ev == NULL) {
		ERROR("event %s not found", name);
		return;
	}

	/* destroys the event */
	object = json_tokener_parse(data);
	afb_event_push(ev->event, object);
}

/* subscribes an event */
static void api_dbus_client_event_subscribe(struct api_dbus *api, int id, const char *name, uint64_t msgid)
{
	int rc;
	struct dbus_event *ev;
	struct dbus_memo *memo;

	/* retrieves the event */
	ev = api_dbus_client_event_search(api, id, name);
	if (ev == NULL) {
		ERROR("event %s not found", name);
		return;
	}

	/* retrieves the memo */
	memo = api_dbus_client_memo_search(api, msgid);
	if (memo == NULL) {
		ERROR("message not found");
		return;
	}

	/* subscribe the request to the event */
	rc = afb_req_subscribe(memo->req, ev->event);
	if (rc < 0)
		ERROR("can't subscribe: %m");
}

/* unsubscribes an event */
static void api_dbus_client_event_unsubscribe(struct api_dbus *api, int id, const char *name, uint64_t msgid)
{
	int rc;
	struct dbus_event *ev;
	struct dbus_memo *memo;

	/* retrieves the event */
	ev = api_dbus_client_event_search(api, id, name);
	if (ev == NULL) {
		ERROR("event %s not found", name);
		return;
	}

	/* retrieves the memo */
	memo = api_dbus_client_memo_search(api, msgid);
	if (memo == NULL) {
		ERROR("message not found");
		return;
	}

	/* unsubscribe the request from the event */
	rc = afb_req_unsubscribe(memo->req, ev->event);
	if (rc < 0)
		ERROR("can't unsubscribe: %m");
}

/* receives calls for event */
static int api_dbus_client_on_manage_event(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
	const char *eventname, *data;
	int rc;
	int32_t eventid;
	uint8_t order;
	struct api_dbus *api;
	uint64_t msgid;

	/* check if expected message */
	api = userdata;
	if (0 != strcmp(api->name, sd_bus_message_get_interface(m)))
		return 0; /* not the expected interface */
	if (0 != strcmp("event", sd_bus_message_get_member(m)))
		return 0; /* not the expected member */
	if (sd_bus_message_get_expect_reply(m))
		return 0; /* not the expected type of message */

	/* reads the message */
	rc = sd_bus_message_read(m, "yisst", &order, &eventid, &eventname, &data, &msgid);
	if (rc < 0) {
		ERROR("unreadable event");
		return 1;
	}

	/* what is the order ? */
	switch ((char)order) {
	case '+': /* creates the event */
		api_dbus_client_event_create(api, eventid, eventname);
		break;
	case '-': /* drops the event */
		api_dbus_client_event_drop(api, eventid, eventname);
		break;
	case '!': /* pushs the event */
		api_dbus_client_event_push(api, eventid, eventname, data);
		break;
	case 'S': /* subscribe event for a request */
		api_dbus_client_event_subscribe(api, eventid, eventname, msgid);
		break;
	case 'U': /* unsubscribe event for a request */
		api_dbus_client_event_unsubscribe(api, eventid, eventname, msgid);
		break;
	default:
		/* unexpected order */
		ERROR("unexpected order '%c' received", (char)order);
		break;
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

	/* connect to broadcasted events */
	rc = asprintf(&match, "type='signal',path='%s',interface='%s',member='broadcast'", api->path, api->name);
	if (rc < 0) {
		errno = ENOMEM;
		ERROR("out of memory");
		goto error;
	}
	rc = sd_bus_add_match(api->sdbus, &api->client.slot_broadcast, match, api_dbus_client_on_broadcast_event, api);
	free(match);
	if (rc < 0) {
		errno = -rc;
		ERROR("can't add dbus match %s for %s", api->path, api->name);
		goto error;
	}

	/* connect to event management */
	rc = sd_bus_add_object(api->sdbus, &api->client.slot_event, api->path, api_dbus_client_on_manage_event, api);
	if (rc < 0) {
		errno = -rc;
		ERROR("can't add dbus object %s for %s", api->path, api->name);
		goto error;
	}

	/* record it as an API */
	afb_api.closure = api;
	afb_api.call = (void*)api_dbus_client_call;
	afb_api.service_start = (void*)api_dbus_service_start;
	if (afb_apis_add(api->api, afb_api) < 0)
		goto error2;

	return 0;

error2:
	destroy_api_dbus(api);
error:
	return -1;
}

/******************* event structures for server part **********************************/

static void afb_api_dbus_server_event_add(void *closure, const char *event, int eventid);
static void afb_api_dbus_server_event_remove(void *closure, const char *event, int eventid);
static void afb_api_dbus_server_event_push(void *closure, const char *event, int eventid, struct json_object *object);
static void afb_api_dbus_server_event_broadcast(void *closure, const char *event, int eventid, struct json_object *object);

/* the interface for events broadcasting */
static const struct afb_evt_itf evt_broadcast_itf = {
	.broadcast = afb_api_dbus_server_event_broadcast,
};

/* the interface for events pushing */
static const struct afb_evt_itf evt_push_itf = {
	.push = afb_api_dbus_server_event_push,
	.add = afb_api_dbus_server_event_add,
	.remove = afb_api_dbus_server_event_remove
};

/******************* destination description part for server *****************************/

struct destination
{
	/* link to next different destination */
	struct destination *next;

	/* the server dbus-api */
	struct api_dbus *api;

	/* count of references */
	int refcount;

	/* the destination */
	char name[1];
};

static struct destination *afb_api_dbus_server_destination_get(struct api_dbus *api, const char *sender)
{
	struct destination *destination;

	/* searchs for an existing destination */
	destination = api->server.destinations;
	while (destination != NULL) {
		if (0 == strcmp(destination->name, sender)) {
			destination->refcount++;
			return destination;
		}
		destination = destination->next;
	}

	/* not found, create it */
	destination = malloc(strlen(sender) + sizeof *destination);
	if (destination == NULL)
		errno = ENOMEM;
	else {
		destination->api = api;
		destination->refcount = 1;
		strcpy(destination->name, sender);
		destination->next = api->server.destinations;
		api->server.destinations = destination;
	}
	return destination;
}

static void afb_api_dbus_server_destination_unref(struct destination *destination)
{
	if (!--destination->refcount) {
		struct destination **prv;

		prv = &destination->api->server.destinations;
		while(*prv != destination)
			prv = &(*prv)->next;
		*prv = destination->next;
		free(destination);
	}
}

struct listener
{
	/* link to next different destination */
	struct destination *destination;

	/* the listener of events */
	struct afb_evt_listener *listener;
};

static void afb_api_dbus_server_listener_free(struct listener *listener)
{
	afb_evt_listener_unref(listener->listener);
	afb_api_dbus_server_destination_unref(listener->destination);
	free(listener);
}

static struct listener *afb_api_dbus_server_listerner_get(struct api_dbus *api, const char *sender, struct AFB_clientCtx *session)
{
	int rc;
	struct listener *listener;
	struct destination *destination;

	/* get the destination */
	destination = afb_api_dbus_server_destination_get(api, sender);
	if (destination == NULL)
		return NULL;

	/* retrieves the stored listener */
	listener = ctxClientCookieGet(session, destination);
	if (listener != NULL) {
		/* found */
		afb_api_dbus_server_destination_unref(destination);
		return listener;
	}

	/* creates the listener */
	listener = malloc(sizeof *listener);
	if (listener == NULL)
		errno = ENOMEM;
	else {
		listener->destination = destination;
		listener->listener = afb_evt_listener_create(&evt_push_itf, destination);
		if (listener->listener != NULL) {
			rc = ctxClientCookieSet(session, destination, listener, (void*)afb_api_dbus_server_listener_free);
			if (rc == 0)
				return listener;
			afb_evt_listener_unref(listener->listener);
		}
		free(listener);
	}
	afb_api_dbus_server_destination_unref(destination);
	return NULL;
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
	struct listener *listener;	/* the listener for events */
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
		if (dreq->json == NULL && strcmp(dreq->request, "null")) {
			/* lazy error detection of json request. Is it to improve? */
			dreq->json = json_object_new_string(dreq->request);
		}
	}
	return dreq->json;
}

/* get the argument of the request of 'name' */
static struct afb_arg dbus_req_get(struct dbus_req *dreq, const char *name)
{
	return afb_msg_json_get_arg(dbus_req_json(dreq), name);
}

static void dbus_req_reply(struct dbus_req *dreq, uint8_t type, const char *first, const char *second)
{
	int rc;
	rc = sd_bus_reply_method_return(dreq->message,
			"yssu", type, first ? : "", second ? : "", (uint32_t)dreq->context.flags);
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

static void afb_api_dbus_server_event_send(struct destination *destination, char order, const char *event, int eventid, const char *data, uint64_t msgid);

static int dbus_req_subscribe(struct dbus_req *dreq, struct afb_event event)
{
	uint64_t msgid;
	int rc;

	rc = afb_evt_add_watch(dreq->listener->listener, event);
	sd_bus_message_get_cookie(dreq->message, &msgid);
	afb_api_dbus_server_event_send(dreq->listener->destination, 'S', afb_evt_event_name(event), afb_evt_event_id(event), "", msgid);
	return rc;
}

static int dbus_req_unsubscribe(struct dbus_req *dreq, struct afb_event event)
{
	uint64_t msgid;
	int rc;

	sd_bus_message_get_cookie(dreq->message, &msgid);
	afb_api_dbus_server_event_send(dreq->listener->destination, 'U', afb_evt_event_name(event), afb_evt_event_id(event), "", msgid);
	rc = afb_evt_remove_watch(dreq->listener->listener, event);
	return rc;
}

static void dbus_req_subcall(struct dbus_req *dreq, const char *api, const char *verb, struct json_object *args, void (*callback)(void*, int, struct json_object*), void *closure);

const struct afb_req_itf afb_api_dbus_req_itf = {
	.json = (void*)dbus_req_json,
	.get = (void*)dbus_req_get,
	.success = (void*)dbus_req_success,
	.fail = (void*)dbus_req_fail,
	.raw = (void*)dbus_req_raw,
	.send = (void*)dbus_req_send,
	.context_get = (void*)afb_context_get,
	.context_set = (void*)afb_context_set,
	.addref = (void*)dbus_req_addref,
	.unref = (void*)dbus_req_unref,
	.session_close = (void*)afb_context_close,
	.session_set_LOA = (void*)afb_context_change_loa,
	.subscribe = (void*)dbus_req_subscribe,
	.unsubscribe = (void*)dbus_req_unsubscribe,
	.subcall = (void*)dbus_req_subcall
};

static void dbus_req_subcall(struct dbus_req *dreq, const char *api, const char *verb, struct json_object *args, void (*callback)(void*, int, struct json_object*), void *closure)
{
	afb_subcall(&dreq->context, api, verb, args, callback, closure, (struct afb_req){ .itf = &afb_api_dbus_req_itf, .closure = dreq });
}

/******************* server part **********************************/

static void afb_api_dbus_server_event_send(struct destination *destination, char order, const char *event, int eventid, const char *data, uint64_t msgid)
{
	int rc;
	struct api_dbus *api;
	struct sd_bus_message *msg;

	api = destination->api;
	msg = NULL;

	rc = sd_bus_message_new_method_call(api->sdbus, &msg, destination->name, api->path, api->name, "event");
	if (rc < 0)
		goto error;

	rc = sd_bus_message_append(msg, "yisst", (uint8_t)order, (int32_t)eventid, event, data, msgid);
	if (rc < 0)
		goto error;

	rc = sd_bus_send(api->sdbus, msg, NULL); /* NULL for cookie implies no expected reply */
	if (rc >= 0)
		goto end;

error:
	ERROR("error while send event %c%s(%d) to %s", order, event, eventid, destination->name);
end:
	sd_bus_message_unref(msg);
}

static void afb_api_dbus_server_event_add(void *closure, const char *event, int eventid)
{
	afb_api_dbus_server_event_send(closure, '+', event, eventid, "", 0);
}

static void afb_api_dbus_server_event_remove(void *closure, const char *event, int eventid)
{
	afb_api_dbus_server_event_send(closure, '-', event, eventid, "", 0);
}

static void afb_api_dbus_server_event_push(void *closure, const char *event, int eventid, struct json_object *object)
{
	const char *data = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
	afb_api_dbus_server_event_send(closure, '!', event, eventid, data, 0);
	json_object_put(object);
}

static void afb_api_dbus_server_event_broadcast(void *closure, const char *event, int eventid, struct json_object *object)
{
	int rc;
	struct api_dbus *api;

	api = closure;
	rc = sd_bus_emit_signal(api->sdbus, api->path, api->name, "broadcast",
			"ss", event, json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN));
	if (rc < 0)
		ERROR("error while broadcasting event %s", event);
	json_object_put(object);
}

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
	struct AFB_clientCtx *session;
	struct listener *listener;

	/* check the interface */
	if (strcmp(sd_bus_message_get_interface(message), api->name) != 0)
		return 0;

	/* get the method */
	method = sd_bus_message_get_member(message);

	/* create the request */
	dreq = calloc(1 , sizeof *dreq);
	if (dreq == NULL)
		goto out_of_memory;

	/* get the data */
	rc = sd_bus_message_read(message, "ssu", &dreq->request, &uuid, &flags);
	if (rc < 0) {
		sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_SIGNATURE, "invalid signature");
		goto error;
	}

	/* connect to the context */
	if (afb_context_connect(&dreq->context, uuid, NULL) < 0)
		goto out_of_memory;
	session = dreq->context.session;

	/* get the listener */
	listener = afb_api_dbus_server_listerner_get(api, sd_bus_message_get_sender(message), session);
	if (listener == NULL)
		goto out_of_memory;

	/* fulfill the request and emit it */
	dreq->context.flags = flags;
	dreq->message = sd_bus_message_ref(message);
	dreq->json = NULL;
	dreq->listener = listener;
	dreq->refcount = 1;
	areq.itf = &afb_api_dbus_req_itf;
	areq.closure = dreq;
	afb_apis_call_(areq, &dreq->context, api->api, method);
	dbus_req_unref(dreq);
	return 1;

out_of_memory:
	sd_bus_reply_method_errorf(message, SD_BUS_ERROR_NO_MEMORY, "out of memory");
error:
	free(dreq);
	return 1;
}

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
	rc = sd_bus_add_object(api->sdbus, &api->server.slot_call, api->path, api_dbus_server_on_object_called, api);
	if (rc < 0) {
		errno = -rc;
		ERROR("can't add dbus object %s for %s", api->path, api->name);
		goto error3;
	}
	INFO("afb service over dbus installed, name %s, path %s", api->name, api->path);

	api->server.listener = afb_evt_listener_create(&evt_broadcast_itf, api);
	return 0;
error3:
	sd_bus_release_name(api->sdbus, api->name);
error2:
	destroy_api_dbus(api);
error:
	return -1;
}


