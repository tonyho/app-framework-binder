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


/*
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <search.h>
#include <assert.h>
*/

#include "afb-apis.h"
#include "session.h"

#define NOW (time(NULL))

// Session UUID are store in a simple array [for 10 sessions this should be enough]
static struct {
  pthread_mutex_t mutex;          // declare a mutex to protect hash table
  AFB_clientCtx **store;          // sessions store
  int count;                      // current number of sessions
  int max;
  int timeout;
  int apicount;
  const char *initok;
} sessions;

static const char key_uuid[] = "uuid";
static const char key_token[] = "token";

// Free context [XXXX Should be protected again memory abort XXXX]
static void ctxUuidFreeCB (AFB_clientCtx *client)
{
    int idx;

    // If application add a handle let's free it now
    if (client->contexts != NULL) {

        // Free client handle with a standard Free function, with app callback or ignore it
        for (idx=0; idx < sessions.apicount; idx ++) {
            if (client->contexts[idx] != NULL) {
		afb_apis_free_context(idx, client->contexts[idx]);
            }
        }
    }
}

// Create a new store in RAM, not that is too small it will be automatically extended
void ctxStoreInit (int nbSession, int timeout, int apicount, const char *initok)
{
	// let's create as store as hashtable does not have any
	sessions.store = calloc (1 + (unsigned)nbSession, sizeof(AFB_clientCtx));
	sessions.max = nbSession;
	sessions.timeout = timeout;
	sessions.apicount = apicount;
	if (strlen(initok) >= 37) {
		fprintf(stderr, "Error: initial token '%s' too long (max length 36)", initok);
		exit(1);
	}
	sessions.initok = initok;
}

static AFB_clientCtx *ctxStoreSearch (const char* uuid)
{
    int  idx;
    AFB_clientCtx *client;

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

static int ctxStoreDel (AFB_clientCtx *client)
{
    int idx;
    int status;

    assert (client != NULL);

    pthread_mutex_lock(&sessions.mutex);

    for (idx=0; idx < sessions.max; idx++) {
        if (sessions.store[idx] == client) {
	        sessions.store[idx]=NULL;
        	sessions.count--;
	        ctxUuidFreeCB (client);
	        status = 1;
		goto deleted;
	}
    }
    status = 0;
deleted:
    pthread_mutex_unlock(&sessions.mutex);
    return status;
}

static int ctxStoreAdd (AFB_clientCtx *client)
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
static int ctxStoreTooOld (AFB_clientCtx *ctx, time_t now)
{
    return ctx->timeStamp <= now;
}

// Loop on every entry and remove old context sessions.hash
void ctxStoreGarbage ()
{
    AFB_clientCtx *ctx;
    long idx;
    time_t now = NOW;

    // Loop on Sessions Table and remove anything that is older than timeout
    for (idx=0; idx < sessions.max; idx++) {
        ctx = sessions.store[idx];
        if (ctx != NULL && ctxStoreTooOld(ctx, now)) {
            ctxStoreDel (ctx);
        }
    }
}

// This function will return exiting client context or newly created client context
AFB_clientCtx *ctxClientGet (const char *uuid)
{
	uuid_t newuuid;
	AFB_clientCtx *clientCtx;

	/* search for an existing one not too old */
	clientCtx = uuid != NULL ? ctxStoreSearch (uuid) : NULL;
	if (clientCtx) {
            if (!ctxStoreTooOld (clientCtx, NOW))
		return clientCtx;
            ctxStoreDel (clientCtx);
        }

	/* mimic old behaviour */
	if (sessions.initok == NULL)
		return NULL;

    	/* cleanup before creating */
	if(2 * sessions.count >= sessions.max)
		ctxStoreGarbage();

	/* returns a new one */
        clientCtx = calloc(1, sizeof(AFB_clientCtx)); // init NULL clientContext
	if (clientCtx != NULL) {
	        clientCtx->contexts = calloc ((unsigned)sessions.apicount, sizeof (void*));
		if (clientCtx->contexts != NULL) {
			/* generate the uuid */
			uuid_generate(newuuid);
			uuid_unparse_lower(newuuid, clientCtx->uuid);
    			clientCtx->timeStamp = time(NULL) + sessions.timeout;
			strcpy(clientCtx->token, sessions.initok);
			if (ctxStoreAdd (clientCtx))
				return clientCtx;
			free(clientCtx->contexts);
		}
		free(clientCtx);
	}
	return NULL;
}

// Free Client Session Context
int ctxClientClose (AFB_clientCtx *clientCtx)
{
	assert(clientCtx != NULL);
	return ctxStoreDel (clientCtx);
}

// Sample Generic Ping Debug API
int ctxTokenCheck (AFB_clientCtx *clientCtx, const char *token)
{
	assert(clientCtx != NULL);
	assert(token != NULL);

	// compare current token with previous one
	if (ctxStoreTooOld (clientCtx, NOW))
		return 0;
	if (!clientCtx->token[0] || 0 == strcmp (token, clientCtx->token)) {
		clientCtx->timeStamp = time(NULL) + sessions.timeout;
		return 1;
	}

	// Token is not valid let move level of assurance to zero and free attached client handle
	return 0;
}

// generate a new token and update client context
int ctxTokenNew (AFB_clientCtx *clientCtx)
{
	uuid_t newuuid;

	assert(clientCtx != NULL);

	// Old token was valid let's regenerate a new one
	uuid_generate(newuuid);         // create a new UUID
	uuid_unparse_lower(newuuid, clientCtx->token);

	// keep track of time for session timeout and further clean up
	clientCtx->timeStamp = time(NULL) + sessions.timeout;

	return 1;
}

