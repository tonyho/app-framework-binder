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

#define _GNU_SOURCE

#include <stdlib.h>

#include <json-c/json.h>

#include <afb/afb-req-itf.h>
#include <afb/afb-service-itf.h>

#include "session.h"
#include "afb-context.h"
#include "afb-evt.h"
#include "afb-subcall.h"
#include "afb-svc.h"

/*
 * Structure for recording service
 */
struct afb_svc
{
	/* session of the service */
	struct AFB_clientCtx *session;

	/* event listener of the service or NULL */
	struct afb_evt_listener *listener;

	/* on event callback for the service */
	void (*on_event)(const char *event, struct json_object *object);
};

/*
 * Structure for requests initiated by the service
 */
struct svc_req
{
	/*
	 * CAUTION: 'context' field should be the first because there
	 * is an implicit convertion to struct afb_context
	 */
	struct afb_context context;

	/* the service */
	struct afb_svc *svc;

	/* the count of references to the request */
	int refcount;
};

/* functions for services */
static void svc_on_event(struct afb_svc *svc, const char *event, int eventid, struct json_object *object);
static void svc_call(struct afb_svc *svc, const char *api, const char *verb, struct json_object *args,
				void (*callback)(void*, int, struct json_object*), void *closure);

/* the interface for services */
static const struct afb_service_itf service_itf = {
	.call = (void*)svc_call
};

/* the interface for events */
static const struct afb_evt_itf evt_itf = {
	.broadcast = (void*)svc_on_event,
	.push = (void*)svc_on_event
};

/* functions for requests of services */
static void svcreq_addref(struct svc_req *svcreq);
static void svcreq_unref(struct svc_req *svcreq);
static int svcreq_subscribe(struct svc_req *svcreq, struct afb_event event);
static int svcreq_unsubscribe(struct svc_req *svcreq, struct afb_event event);
static void svcreq_subcall(struct svc_req *svcreq, const char *api, const char *verb, struct json_object *args,
				void (*callback)(void*, int, struct json_object*), void *closure);

/* interface for requests of services */
const struct afb_req_itf afb_svc_req_itf = {
	.addref = (void*)svcreq_addref,
	.unref = (void*)svcreq_unref,
	.context_get = (void*)afb_context_get,
	.context_set = (void*)afb_context_set,
	.session_close = (void*)afb_context_close,
	.session_set_LOA = (void*)afb_context_change_loa,
	.subscribe = (void*)svcreq_subscribe,
	.unsubscribe = (void*)svcreq_unsubscribe,
	.subcall = (void*)svcreq_subcall
};

/* the common session for services sahring their session */
static struct AFB_clientCtx *common_session;

/*
 * Creates a new service
 */
struct afb_svc *afb_svc_create(int share_session, int (*init)(struct afb_service service), void (*on_event)(const char *event, struct json_object *object))
{
	int rc;
	struct afb_svc *svc;

	/* allocates the svc handler */
	svc = malloc(sizeof * svc);
	if (svc == NULL)
		goto error;

	/* instanciate the session */
	if (share_session) {
		/* session shared with other svcs */
		if (common_session == NULL) {
			common_session = ctxClientCreate (NULL, 0);
			if (common_session == NULL)
				goto error2;
		}
		svc->session = ctxClientAddRef(common_session);
	} else {
		/* session dedicated to the svc */
		svc->session = ctxClientCreate (NULL, 0);
		if (svc->session == NULL)
			goto error2;
	}

	/* initialises the listener if needed */
	if (on_event == NULL)
		svc->listener = NULL;
	else {
		svc->listener = afb_evt_listener_create(&evt_itf, svc);
		if (svc->listener == NULL)
			goto error3;
	}

	/* initialises the svc now */
	rc = init((struct afb_service){ .itf = &service_itf, .closure = svc });
	if (rc < 0)
		goto error4;

	return svc;

error4:
	if (svc->listener != NULL)
		afb_evt_listener_unref(svc->listener);
error3:
	ctxClientUnref(svc->session);
error2:
	free(svc);
error:
	return NULL;
}

/*
 * Propagates the event to the service
 */
static void svc_on_event(struct afb_svc *svc, const char *event, int eventid, struct json_object *object)
{
	svc->on_event(event, object);
}

/*
 * Initiates a call for the service
 */
static void svc_call(struct afb_svc *svc, const char *api, const char *verb, struct json_object *args, void (*callback)(void*, int, struct json_object*), void *closure)
{
	struct svc_req *svcreq;

	/* allocates the request */
	svcreq = malloc(sizeof *svcreq);
	if (svcreq == NULL)
		return afb_subcall_internal_error(callback, closure);

	/* initialises the request */
	afb_context_init(&svcreq->context, svc->session, NULL);
	svcreq->context.validated = 1;
	svcreq->svc = svc;
	svcreq->refcount = 1;

	/* makes the call */
	afb_subcall(&svcreq->context, api, verb, args, callback, closure, (struct afb_req){ .itf = &afb_svc_req_itf, .closure = svcreq });

	/* terminates and frees ressources if needed */
	svcreq_unref(svcreq);
}

static void svcreq_addref(struct svc_req *svcreq)
{
	svcreq->refcount++;
}

static void svcreq_unref(struct svc_req *svcreq)
{
	if (0 == --svcreq->refcount) {
		afb_context_disconnect(&svcreq->context);
		free(svcreq);
	}
}

static int svcreq_subscribe(struct svc_req *svcreq, struct afb_event event)
{
	if (svcreq->svc->listener == NULL)
		return -1;
	return afb_evt_add_watch(svcreq->svc->listener, event);
}

static int svcreq_unsubscribe(struct svc_req *svcreq, struct afb_event event)
{
	if (svcreq->svc->listener == NULL)
		return -1;
	return afb_evt_remove_watch(svcreq->svc->listener, event);
}

static void svcreq_subcall(struct svc_req *svcreq, const char *api, const char *verb, struct json_object *args, void (*callback)(void*, int, struct json_object*), void *closure)
{
	afb_subcall(&svcreq->context, api, verb, args, callback, closure, (struct afb_req){ .itf = &afb_svc_req_itf, .closure = svcreq });
}

