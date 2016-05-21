/*
 * Copyright (C) 2015, 2016 "IoT.bzh"
 * Author "Fulup Ar Foll"
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
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>
#include <assert.h>
#include <errno.h>

#include <json-c/json.h>

#include "session.h"
#include "verbose.h"

#define NOW (time(NULL))

struct client_value
{
	void *value;
	void (*free_value)(void*);
};

struct afb_event_listener_list
{
	struct afb_event_listener_list *next;
	struct afb_event_listener listener;
	int refcount;
};

struct AFB_clientCtx
{
	unsigned refcount;
	unsigned loa;
	time_t expiration;    // expiration time of the token
	time_t access;
	char uuid[37];        // long term authentication of remote client
	char token[37];       // short term authentication of remote client
	struct client_value *values;
	struct afb_event_listener_list *listeners;
};

// Session UUID are store in a simple array [for 10 sessions this should be enough]
static struct {
  pthread_mutex_t mutex;          // declare a mutex to protect hash table
  struct AFB_clientCtx **store;          // sessions store
  int count;                      // current number of sessions
  int max;
  int timeout;
  int apicount;
  char initok[37];
  struct afb_event_listener_list *listeners;
} sessions;

/* generate a uuid */
static void new_uuid(char uuid[37])
{
	uuid_t newuuid;
	uuid_generate(newuuid);
	uuid_unparse_lower(newuuid, uuid);
}

// Free context [XXXX Should be protected again memory abort XXXX]
static void ctxUuidFreeCB (struct AFB_clientCtx *client)
{
	int idx;

	// If application add a handle let's free it now
	assert (client->values != NULL);

	// Free client handle with a standard Free function, with app callback or ignore it
	for (idx=0; idx < sessions.apicount; idx ++)
		ctxClientValueSet(client, idx, NULL, NULL);
}

// Create a new store in RAM, not that is too small it will be automatically extended
void ctxStoreInit (int max_session_count, int timeout, const char *initok, int context_count)
{
	// let's create as store as hashtable does not have any
	sessions.store = calloc (1 + (unsigned)max_session_count, sizeof(struct AFB_clientCtx));
	sessions.max = max_session_count;
	sessions.timeout = timeout;
	sessions.apicount = context_count;
	if (initok == NULL)
		/* without token, a secret is made to forbid creation of sessions */
		new_uuid(sessions.initok);
	else if (strlen(initok) < sizeof(sessions.store[0]->token))
		strcpy(sessions.initok, initok);
	else {
		ERROR("initial token '%s' too long (max length 36)", initok);
		exit(1);
	}
}

static struct AFB_clientCtx *ctxStoreSearch (const char* uuid)
{
    int  idx;
    struct AFB_clientCtx *client;

    assert (uuid != NULL);

    pthread_mutex_lock(&sessions.mutex);

    for (idx=0; idx < sessions.max; idx++) {
	client = sessions.store[idx];
        if (client && (0 == strcmp (uuid, client->uuid)))
		goto found;
    }
    client = NULL;

found:
    pthread_mutex_unlock(&sessions.mutex);
    return client;
}

static int ctxStoreDel (struct AFB_clientCtx *client)
{
    int idx;
    int status;

    assert (client != NULL);

    pthread_mutex_lock(&sessions.mutex);

    for (idx=0; idx < sessions.max; idx++) {
        if (sessions.store[idx] == client) {
	        sessions.store[idx] = NULL;
        	sessions.count--;
	        status = 1;
		goto deleted;
	}
    }
    status = 0;
deleted:
    pthread_mutex_unlock(&sessions.mutex);
    return status;
}

static int ctxStoreAdd (struct AFB_clientCtx *client)
{
    int idx;
    int status;

    assert (client != NULL);

    pthread_mutex_lock(&sessions.mutex);

    for (idx=0; idx < sessions.max; idx++) {
        if (NULL == sessions.store[idx]) {
        	sessions.store[idx] = client;
	        sessions.count++;
        	status = 1;
		goto added;
	}
    }
    status = 0;
added:
    pthread_mutex_unlock(&sessions.mutex);
    return status;
}

// Check if context timeout or not
static int ctxStoreTooOld (struct AFB_clientCtx *ctx, time_t now)
{
    assert (ctx != NULL);
    return ctx->expiration < now;
}

// Check if context is active or not
static int ctxIsActive (struct AFB_clientCtx *ctx, time_t now)
{
    assert (ctx != NULL);
    return ctx->uuid[0] != 0 && ctx->expiration >= now;
}

// Loop on every entry and remove old context sessions.hash
static void ctxStoreCleanUp (time_t now)
{
	struct AFB_clientCtx *ctx;
	long idx;

	// Loop on Sessions Table and remove anything that is older than timeout
	for (idx=0; idx < sessions.max; idx++) {
		ctx = sessions.store[idx];
		if (ctx != NULL && ctxStoreTooOld(ctx, now)) {
			ctxClientClose (ctx);
		}
	}
}

// This function will return exiting client context or newly created client context
struct AFB_clientCtx *ctxClientGetSession (const char *uuid, int *created)
{
	struct AFB_clientCtx *clientCtx;
	time_t now;

	/* cleaning */
	now = NOW;
	ctxStoreCleanUp (now);

	/* search for an existing one not too old */
	if (uuid != NULL) {
		if (strlen(uuid) >= sizeof clientCtx->uuid) {
			errno = EINVAL;
			goto error;
		}
		clientCtx = ctxStoreSearch(uuid);
		if (clientCtx != NULL) {
			*created = 0;
			goto found;
		}
	}

	/* returns a new one */
        clientCtx = calloc(1, sizeof(struct AFB_clientCtx) + ((unsigned)sessions.apicount * sizeof(*clientCtx->values)));
	if (clientCtx == NULL) {
		errno = ENOMEM;
		goto error;
	}
        clientCtx->values = (void*)(clientCtx + 1);

	/* generate the uuid */
	if (uuid == NULL) {
		new_uuid(clientCtx->uuid);
	} else {
		strcpy(clientCtx->uuid, uuid);
	}

	/* init the token */
	strcpy(clientCtx->token, sessions.initok);
	clientCtx->expiration = now + sessions.timeout;
	if (!ctxStoreAdd (clientCtx)) {
		errno = ENOMEM;
		goto error2;
	}
	*created = 1;

found:
	clientCtx->access = now;
	clientCtx->refcount++;
	return clientCtx;

error2:
	free(clientCtx);
error:
	return NULL;
}

struct AFB_clientCtx *ctxClientAddRef(struct AFB_clientCtx *clientCtx)
{
	if (clientCtx != NULL)
		clientCtx->refcount++;
	return clientCtx;
}

void ctxClientUnref(struct AFB_clientCtx *clientCtx)
{
	if (clientCtx != NULL) {
		assert(clientCtx->refcount != 0);
		--clientCtx->refcount;
       		if (clientCtx->refcount == 0 && clientCtx->uuid[0] == 0) {
			ctxStoreDel (clientCtx);
			free(clientCtx);
		}
	}
}

// Free Client Session Context
void ctxClientClose (struct AFB_clientCtx *clientCtx)
{
	assert(clientCtx != NULL);
	if (clientCtx->uuid[0] != 0) {
		clientCtx->uuid[0] = 0;
	        ctxUuidFreeCB (clientCtx);
		while(clientCtx->listeners != NULL)
			ctxClientEventListenerRemove(clientCtx, clientCtx->listeners->listener);
       		if (clientCtx->refcount == 0) {
			ctxStoreDel (clientCtx);
			free(clientCtx);
		}
	}
}

// Sample Generic Ping Debug API
int ctxTokenCheck (struct AFB_clientCtx *clientCtx, const char *token)
{
	assert(clientCtx != NULL);
	assert(token != NULL);

	// compare current token with previous one
	if (!ctxIsActive (clientCtx, NOW))
		return 0;

	if (clientCtx->token[0] && strcmp (token, clientCtx->token) != 0)
		return 0;

	return 1;
}

// generate a new token and update client context
void ctxTokenNew (struct AFB_clientCtx *clientCtx)
{
	assert(clientCtx != NULL);

	// Old token was valid let's regenerate a new one
	new_uuid(clientCtx->token);

	// keep track of time for session timeout and further clean up
	clientCtx->expiration = NOW + sessions.timeout;
}

static int add_listener(struct afb_event_listener_list **head, struct afb_event_listener listener)
{
	struct afb_event_listener_list *iter, **prv;

	prv = head;
	for (;;) {
		iter = *prv;
		if (iter == NULL) {
			iter = calloc(1, sizeof *iter);
			if (iter == NULL) {
				errno = ENOMEM;
				return -1;
			}
			iter->listener = listener;
			iter->refcount = 1;
			*prv = iter;
			return 0;
		}
		if (iter->listener.itf == listener.itf && iter->listener.closure == listener.closure) {
			iter->refcount++;
			return 0;
		}
		prv = &iter->next;
	}
}

int ctxClientEventListenerAdd(struct AFB_clientCtx *clientCtx, struct afb_event_listener listener)
{
	return add_listener(clientCtx != NULL ? &clientCtx->listeners : &sessions.listeners, listener);
}

static void remove_listener(struct afb_event_listener_list **head, struct afb_event_listener listener)
{
	struct afb_event_listener_list *iter, **prv;

	prv = head;
	for (;;) {
		iter = *prv;
		if (iter == NULL)
			return;
		if (iter->listener.itf == listener.itf && iter->listener.closure == listener.closure) {
			if (!--iter->refcount) {
				*prv = iter->next;
				free(iter);
			}
			return;
		}
		prv = &iter->next;
	}
}

void ctxClientEventListenerRemove(struct AFB_clientCtx *clientCtx, struct afb_event_listener listener)
{
	remove_listener(clientCtx != NULL ? &clientCtx->listeners : &sessions.listeners, listener);
}

static int send(struct afb_event_listener_list *head, const char *event, struct json_object *object)
{
	struct afb_event_listener_list *iter;
	int result;

	result = 0;
	iter = head;
	while (iter != NULL) {
		if (iter->listener.itf->expects == NULL || iter->listener.itf->expects(iter->listener.closure, event)) {
			iter->listener.itf->send(iter->listener.closure, event, json_object_get(object));
			result++;
		}
		iter = iter->next;
	}

	return result;
}

int ctxClientEventSend(struct AFB_clientCtx *clientCtx, const char *event, struct json_object *object)
{
	long idx;
	time_t now;
	int result;

	now = NOW;
	if (clientCtx != NULL) {
		result = ctxIsActive(clientCtx, now) ? send(clientCtx->listeners, event, object) : 0;
	} else {
		result = send(sessions.listeners, event, object);
		for (idx=0; idx < sessions.max; idx++) {
			clientCtx = ctxClientAddRef(sessions.store[idx]);
			if (clientCtx != NULL && ctxIsActive(clientCtx, now)) {
				clientCtx = ctxClientAddRef(clientCtx);
				result += send(clientCtx->listeners, event, object);
			}
			ctxClientUnref(clientCtx);
		}
	}
	return result;
}

const char *ctxClientGetUuid (struct AFB_clientCtx *clientCtx)
{
	assert(clientCtx != NULL);
	return clientCtx->uuid;
}

const char *ctxClientGetToken (struct AFB_clientCtx *clientCtx)
{
	assert(clientCtx != NULL);
	return clientCtx->token;
}

unsigned ctxClientGetLOA (struct AFB_clientCtx *clientCtx)
{
	assert(clientCtx != NULL);
	return clientCtx->loa;
}

void ctxClientSetLOA (struct AFB_clientCtx *clientCtx, unsigned loa)
{
	assert(clientCtx != NULL);
	clientCtx->loa = loa;
}

void *ctxClientValueGet(struct AFB_clientCtx *clientCtx, int index)
{
	assert(clientCtx != NULL);
	assert(index >= 0);
	assert(index < sessions.apicount);
	return clientCtx->values[index].value;
}

void ctxClientValueSet(struct AFB_clientCtx *clientCtx, int index, void *value, void (*free_value)(void*))
{
	struct client_value prev;
	assert(clientCtx != NULL);
	assert(index >= 0);
	assert(index < sessions.apicount);
	prev = clientCtx->values[index];
	clientCtx->values[index] = (struct client_value){.value = value, .free_value = free_value};
	if (prev.value !=  NULL && prev.free_value != NULL)
		prev.free_value(prev.value);
}
