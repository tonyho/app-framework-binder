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


#include "local-def.h"
#include <dirent.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <search.h>
#include <assert.h>

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

static AFB_error ctxStoreDel (AFB_clientCtx *client)
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
	        status = AFB_SUCCESS;
		goto deleted;
	}
    }
    status = AFB_FAIL;
deleted:
    pthread_mutex_unlock(&sessions.mutex);
    return status;
}

static AFB_error ctxStoreAdd (AFB_clientCtx *client)
{
    int idx;
    int status;
    if (client == NULL)
	return AFB_FAIL;

    //fprintf (stderr, "ctxStoreAdd request uuid=%s count=%d\n", client->uuid, sessions.count);

    pthread_mutex_lock(&sessions.mutex);

    for (idx=0; idx < sessions.max; idx++) {
        if (NULL == sessions.store[idx]) {
        	sessions.store[idx]= client;
	        sessions.count++;
        	status = AFB_SUCCESS;
		goto added;
	}
    }
    status = AFB_FAIL;

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
        if ((ctx != NULL) && (ctxStoreTooOld(ctx, now))) {
            ctxStoreDel (ctx);
        }
    }
}

// This function will return exiting client context or newly created client context
AFB_clientCtx *ctxClientGet (AFB_request *request)
{
  AFB_clientCtx *clientCtx=NULL;
  const char *uuid;
  uuid_t newuuid;

    if (request->config->token == NULL) return NULL;

    // Check if client as a context or not inside the URL
    uuid  = NULL; //MHD_lookup_connection_value(request->connection, MHD_GET_ARGUMENT_KIND, key_uuid);

    // if UUID in query we're restfull with no cookies otherwise check for cookie
    if (uuid != NULL)
        request->restfull = TRUE;
    else {
        char cookie[64];
        request->restfull = FALSE;
        snprintf(cookie, sizeof cookie, "%s-%d", COOKIE_NAME, request->config->httpdPort);
        uuid = NULL; //MHD_lookup_connection_value (request->connection, MHD_COOKIE_KIND, cookie);
    };

    // Warning when no cookie defined MHD_lookup_connection_value may return something !!!
    if ((uuid != NULL) && (strnlen (uuid, 10) >= 10))   {
        // search if client context exist and it not timeout let's use it
        clientCtx = ctxStoreSearch (uuid);

	if (clientCtx) {
            if (ctxStoreTooOld (clientCtx, NOW)) {
                 // this session is too old let's delete it
                ctxStoreDel (clientCtx);
                clientCtx = NULL;
            } else {
                return clientCtx;
            }
        }
    }

    // we have no session let's create one otherwise let's clean any exiting values
    if (clientCtx == NULL) {
        clientCtx = calloc(1, sizeof(AFB_clientCtx)); // init NULL clientContext
        clientCtx->contexts = calloc ((unsigned)sessions.apicount, sizeof (void*));
    }

    uuid_generate(newuuid);         // create a new UUID
    uuid_unparse_lower(newuuid, clientCtx->uuid);

    // if table is full at 50% let's clean it up
    if(sessions.count > (sessions.max / 2)) ctxStoreGarbage();

    // finally add uuid into hashtable
    if (AFB_SUCCESS != ctxStoreAdd (clientCtx)) {
        free (clientCtx);
        return NULL;
    }
    return clientCtx;
}

// Sample Generic Ping Debug API
AFB_error ctxTokenCheck (AFB_clientCtx *clientCtx, AFB_request *request)
{
    const char *token;

    if (clientCtx->contexts == NULL)
	return AFB_EMPTY;

    // this time have to extract token from query list
    token = NULL; //MHD_lookup_connection_value(request->connection, MHD_GET_ARGUMENT_KIND, key_token);

    // if not token is providing we refuse the exchange
    if ((token == NULL) || (clientCtx->token == NULL))
	return AFB_FALSE;

    // compare current token with previous one
    if ((0 == strcmp (token, clientCtx->token)) && (!ctxStoreTooOld (clientCtx, NOW))) {
       return AFB_SUCCESS;
    }

    // Token is not valid let move level of assurance to zero and free attached client handle
    return AFB_FAIL;
}

// Free Client Session Context
AFB_error ctxTokenReset (AFB_clientCtx *clientCtx, AFB_request *request)
{
    if (clientCtx == NULL)
       return AFB_EMPTY;
    //if (verbose) fprintf (stderr, "ctxClientReset New uuid=[%s] token=[%s] timestamp=%d\n", clientCtx->uuid, clientCtx->token, clientCtx->timeStamp);

    // Search for an existing client with the same UUID
    clientCtx = ctxStoreSearch (clientCtx->uuid);
    if (clientCtx == NULL)
       return AFB_FALSE;

    // Remove client from table
    ctxStoreDel (clientCtx);

    return AFB_SUCCESS;
}

// generate a new token
AFB_error ctxTokenCreate (AFB_clientCtx *clientCtx, AFB_request *request)
{
    uuid_t newuuid;
    const char *token;

    if (clientCtx == NULL)
       return AFB_EMPTY;

    // if config->token!="" then verify that we have the right initial share secret
    if (request->config->token[0] != '\0') {

        // check for initial token secret and return if not presented
        token = NULL; //MHD_lookup_connection_value(request->connection, MHD_GET_ARGUMENT_KIND, key_token);
        if (token == NULL)
           return AFB_UNAUTH;

        // verify that it fits with initial tokens fit
        if (strcmp(request->config->token, token))
           return AFB_UNAUTH;
    }

    // create a UUID as token value
    uuid_generate(newuuid);
    uuid_unparse_lower(newuuid, clientCtx->token);

    // keep track of time for session timeout and further clean up
    clientCtx->timeStamp = time(NULL) + sessions.timeout;

    // Token is also store in context but it might be convenient for plugin to access it directly
    return AFB_SUCCESS;
}


// generate a new token and update client context
AFB_error ctxTokenRefresh (AFB_clientCtx *clientCtx, AFB_request *request)
{
    uuid_t newuuid;

    if (clientCtx == NULL)
        return AFB_EMPTY;

    // Check if the old token is valid
    if (ctxTokenCheck (clientCtx, request) != AFB_SUCCESS)
        return AFB_FAIL;

    // Old token was valid let's regenerate a new one
    uuid_generate(newuuid);         // create a new UUID
    uuid_unparse_lower(newuuid, clientCtx->token);

    // keep track of time for session timeout and further clean up
    clientCtx->timeStamp = time(NULL) + sessions.timeout;

    return AFB_SUCCESS;
}








































































// This function will return exiting client context or newly created client context
AFB_clientCtx *_ctxClientGet (const char *uuid)
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
			if (AFB_SUCCESS == ctxStoreAdd (clientCtx))
				return clientCtx;
			free(clientCtx->contexts);
		}
		free(clientCtx);
	}
	return NULL;
}

// Free Client Session Context
AFB_error _ctxClientDel (AFB_clientCtx *clientCtx)
{
	assert(clientCtx != NULL);
	return ctxStoreDel (clientCtx);
}

// Sample Generic Ping Debug API
AFB_error _ctxTokenCheck (AFB_clientCtx *clientCtx, const char *token)
{
	assert(clientCtx != NULL);
	assert(token != NULL);

	// compare current token with previous one
	if (ctxStoreTooOld (clientCtx, NOW))
		return AFB_FAIL;
	if (!clientCtx->token[0] || 0 == strcmp (token, clientCtx->token)) {
		clientCtx->timeStamp = time(NULL) + sessions.timeout;
		return AFB_SUCCESS;
	}

	// Token is not valid let move level of assurance to zero and free attached client handle
	return AFB_FAIL;
}

// generate a new token and update client context
AFB_error _ctxTokenNew (AFB_clientCtx *clientCtx)
{
	uuid_t newuuid;

	assert(clientCtx != NULL);

	// Old token was valid let's regenerate a new one
	uuid_generate(newuuid);         // create a new UUID
	uuid_unparse_lower(newuuid, clientCtx->token);

	// keep track of time for session timeout and further clean up
	clientCtx->timeStamp = time(NULL) + sessions.timeout;

	return AFB_SUCCESS;
}

