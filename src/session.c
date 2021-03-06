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

struct cookie
{
	struct cookie *next;
	const void *key;
	void *value;
	void (*free_value)(void*);
};

struct AFB_clientCtx
{
	unsigned refcount;
	unsigned loa;
	int timeout;
	time_t expiration;    // expiration time of the token
	time_t access;
	char uuid[37];        // long term authentication of remote client
	char token[37];       // short term authentication of remote client
	struct client_value *values;
	struct cookie *cookies;
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
	struct cookie *cookie;

	// If application add a handle let's free it now
	assert (client->values != NULL);

	// Free client handle with a standard Free function, with app callback or ignore it
	for (idx=0; idx < sessions.apicount; idx ++)
		ctxClientValueSet(client, idx, NULL, NULL);

	// free cookies
	cookie = client->cookies;
	while (cookie != NULL) {
		client->cookies = cookie->next;
		if (cookie->value != NULL && cookie->free_value != NULL)
			cookie->free_value(cookie->value);
		free(cookie);
		cookie = client->cookies;
	}
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

static struct AFB_clientCtx *new_context (const char *uuid, int timeout, time_t now)
{
	struct AFB_clientCtx *clientCtx;

	/* allocates a new one */
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
		if (strlen(uuid) >= sizeof clientCtx->uuid) {
			errno = EINVAL;
			goto error2;
		}
		strcpy(clientCtx->uuid, uuid);
	}

	/* init the token */
	strcpy(clientCtx->token, sessions.initok);
	clientCtx->timeout = timeout;
	if (timeout != 0)
		clientCtx->expiration = now + timeout;
	else {
		clientCtx->expiration = (time_t)(~(time_t)0);
		if (clientCtx->expiration < 0)
			clientCtx->expiration = (time_t)(((unsigned long long)clientCtx->expiration) >> 1);
	}
	if (!ctxStoreAdd (clientCtx)) {
		errno = ENOMEM;
		goto error2;
	}

	clientCtx->access = now;
	clientCtx->refcount = 1;
	return clientCtx;

error2:
	free(clientCtx);
error:
	return NULL;
}

struct AFB_clientCtx *ctxClientCreate (const char *uuid, int timeout)
{
	time_t now;

	/* cleaning */
	now = NOW;
	ctxStoreCleanUp (now);

	/* search for an existing one not too old */
	if (uuid != NULL && ctxStoreSearch(uuid) != NULL) {
		errno = EEXIST;
		return NULL;
	}

	return new_context(uuid, timeout, now);
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
		clientCtx = ctxStoreSearch(uuid);
		if (clientCtx != NULL) {
			*created = 0;
			clientCtx->access = now;
			clientCtx->refcount++;
			return clientCtx;
		}
	}

	*created = 1;
	return new_context(uuid, sessions.timeout, now);
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
	if (clientCtx->timeout != 0)
		clientCtx->expiration = NOW + clientCtx->timeout;
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
	if (prev.value != NULL && prev.value != value && prev.free_value != NULL)
		prev.free_value(prev.value);
}

void *ctxClientCookieGet(struct AFB_clientCtx *clientCtx, const void *key)
{
	struct cookie *cookie;

	cookie = clientCtx->cookies;
	while(cookie != NULL) {
		if (cookie->key == key)
			return cookie->value;
		cookie = cookie->next;
	}
	return NULL;
}

int ctxClientCookieSet(struct AFB_clientCtx *clientCtx, const void *key, void *value, void (*free_value)(void*))
{
	struct cookie *cookie;

	/* search for a replacement */
	cookie = clientCtx->cookies;
	while(cookie != NULL) {
		if (cookie->key == key) {
			if (cookie->value != NULL && cookie->value != value && cookie->free_value != NULL)
				cookie->free_value(cookie->value);
			cookie->value = value;
			cookie->free_value = free_value;
			return 0;
		}
		cookie = cookie->next;
	}

	/* allocates */
	cookie = malloc(sizeof *cookie);
	if (cookie == NULL) {
		errno = ENOMEM;
		return -1;
	}

	cookie->key = key;
	cookie->value = value;
	cookie->free_value = free_value;
	cookie->next = clientCtx->cookies;
	clientCtx->cookies = cookie;
	return 0;
}

