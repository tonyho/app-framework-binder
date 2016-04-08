/*
 * Copyright (C) 2015 "IoT.bzh"
 * Author "Fulup Ar Foll"
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Reference:
 * http://stackoverflow.com/questions/25971505/how-to-delete-element-from-hsearch
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>
#include <assert.h>

#include "afb-apis.h"
#include "session.h"

#define NOW (time(NULL))

// Session UUID are store in a simple array [for 10 sessions this should be enough]
static struct {
  pthread_mutex_t mutex;          // declare a mutex to protect hash table
  struct AFB_clientCtx **store;          // sessions store
  int count;                      // current number of sessions
  int max;
  int timeout;
  int apicount;
  const char *initok;
} sessions;

// Free context [XXXX Should be protected again memory abort XXXX]
static void ctxUuidFreeCB (struct AFB_clientCtx *client)
{
	int idx;

	// If application add a handle let's free it now
	assert (client->contexts != NULL);

	// Free client handle with a standard Free function, with app callback or ignore it
	for (idx=0; idx < sessions.apicount; idx ++) {
		if (client->contexts[idx] != NULL) {
			afb_apis_free_context(idx, client->contexts[idx]);
			client->contexts[idx] = NULL;
		}
	}
}

// Create a new store in RAM, not that is too small it will be automatically extended
void ctxStoreInit (int nbSession, int timeout, const char *initok)
{
	// let's create as store as hashtable does not have any
	sessions.store = calloc (1 + (unsigned)nbSession, sizeof(struct AFB_clientCtx));
	sessions.max = nbSession;
	sessions.timeout = timeout;
	sessions.apicount = afb_apis_count();
	if (strlen(initok) >= 37) {
		fprintf(stderr, "Error: initial token '%s' too long (max length 36)", initok);
		exit(1);
	}
	sessions.initok = initok;
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
	        sessions.store[idx]=NULL;
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

    //fprintf (stderr, "ctxStoreAdd request uuid=%s count=%d\n", client->uuid, sessions.count);

    pthread_mutex_lock(&sessions.mutex);

    for (idx=0; idx < sessions.max; idx++) {
        if (NULL == sessions.store[idx]) {
        	sessions.store[idx]= client;
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
    return ctx->expiration <= now;
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
struct AFB_clientCtx *ctxClientGetForUuid (const char *uuid)
{
	uuid_t newuuid;
	struct AFB_clientCtx *clientCtx;
	time_t now;

	/* search for an existing one not too old */
	now = NOW;
	ctxStoreCleanUp (now);
	clientCtx = uuid != NULL ? ctxStoreSearch (uuid) : NULL;
	if (clientCtx) {
		clientCtx->refcount++;
		return clientCtx;
        }

	/* mimic old behaviour */
	if (sessions.initok == NULL)
		return NULL;

	/* check the uuid if given */
	if (uuid != NULL && 1 + strlen(uuid) >= sizeof clientCtx->uuid)
		return NULL;

	/* returns a new one */
        clientCtx = calloc(1, sizeof(struct AFB_clientCtx)); // init NULL clientContext
	if (clientCtx != NULL) {
	        clientCtx->contexts = calloc ((unsigned)sessions.apicount, sizeof (void*));
		if (clientCtx->contexts != NULL) {
			/* generate the uuid */
			if (uuid == NULL) {
				uuid_generate(newuuid);
				uuid_unparse_lower(newuuid, clientCtx->uuid);
			} else {
				strcpy(clientCtx->uuid, uuid);
			}
			strcpy(clientCtx->token, sessions.initok);
    			clientCtx->expiration = now + sessions.timeout;
			clientCtx->refcount = 1;
			if (ctxStoreAdd (clientCtx))
				return clientCtx;
			free(clientCtx->contexts);
		}
		free(clientCtx);
	}
	return NULL;
}

struct AFB_clientCtx *ctxClientGet(struct AFB_clientCtx *clientCtx)
{
	if (clientCtx != NULL)
		clientCtx->refcount++;
	return clientCtx;
}

void ctxClientPut(struct AFB_clientCtx *clientCtx)
{
	if (clientCtx != NULL) {
		assert(clientCtx->refcount != 0);
		--clientCtx->refcount;
	}
}

// Free Client Session Context
void ctxClientClose (struct AFB_clientCtx *clientCtx)
{
	assert(clientCtx != NULL);
	if (clientCtx->created) {
		clientCtx->created = 0;
	        ctxUuidFreeCB (clientCtx);
	}
       	if (clientCtx->refcount == 0)
		ctxStoreDel (clientCtx);
}

// Sample Generic Ping Debug API
int ctxTokenCheckLen (struct AFB_clientCtx *clientCtx, const char *token, size_t length)
{
	assert(clientCtx != NULL);
	assert(token != NULL);

	// compare current token with previous one
	if (ctxStoreTooOld (clientCtx, NOW))
		return 0;

	if (clientCtx->token[0] && (length >= sizeof(clientCtx->token) || strncmp (token, clientCtx->token, length) || clientCtx->token[length]))
		return 0;

	clientCtx->created = 1; /* creates by default */
	return 1;
}

// Sample Generic Ping Debug API
int ctxTokenCheck (struct AFB_clientCtx *clientCtx, const char *token)
{
	assert(clientCtx != NULL);
	assert(token != NULL);

	return ctxTokenCheckLen(clientCtx, token, strlen(token));
}

// generate a new token and update client context
void ctxTokenNew (struct AFB_clientCtx *clientCtx)
{
	uuid_t newuuid;

	assert(clientCtx != NULL);

	// Old token was valid let's regenerate a new one
	uuid_generate(newuuid);         // create a new UUID
	uuid_unparse_lower(newuuid, clientCtx->token);

	// keep track of time for session timeout and further clean up
	clientCtx->expiration = NOW + sessions.timeout;
}

