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


// Session UUID are store in a simple array [for 10 sessions this should be enough]
static struct {
  pthread_mutex_t mutex;          // declare a mutex to protect hash table
  AFB_clientCtx **store;          // sessions store
  int count;                      // current number of sessions
  int max;
} sessions;

static const char key_uuid = "uuid";
static const char key_token = "token";

// Free context [XXXX Should be protected again memory abort XXXX]
static void ctxUuidFreeCB (AFB_clientCtx *client)
{

    AFB_plugin **plugins = client->plugins;
    AFB_freeCtxCB freeCtxCB;
    int idx;

    // If application add a handle let's free it now
    if (client->contexts != NULL) {

        // Free client handle with a standard Free function, with app callback or ignore it
        for (idx=0; client->plugins[idx] != NULL; idx ++) {
            if (client->contexts[idx] != NULL) {
                freeCtxCB = client->plugins[idx]->freeCtxCB;
                if (freeCtxCB == NULL)
			free (client->contexts[idx]);
                else if (freeCtxCB != (void*)-1)
			freeCtxCB(client->contexts[idx], plugins[idx]->handle, client->uuid);
            }
        }
    }
}

// Create a new store in RAM, not that is too small it will be automatically extended
void ctxStoreInit (int nbSession)
{

   // let's create as store as hashtable does not have any
   sessions.store = calloc (1 + (unsigned)nbSession, sizeof(AFB_clientCtx));
   sessions.max = nbSession;
}

static AFB_clientCtx *ctxStoreSearch (const char* uuid)
{
    int  idx;
    AFB_clientCtx *client;

    if (uuid == NULL)
	return NULL;

    pthread_mutex_lock(&sessions.mutex);

    for (idx=0; idx < sessions.max; idx++) {
        if (sessions.store[idx] && (0 == strcmp (uuid, sessions.store[idx]->uuid))) break;
    }

    if (idx == sessions.max) client=NULL;
    else client= sessions.store[idx];
    pthread_mutex_unlock(&sessions.mutex);

    return client;
}

static AFB_error ctxStoreDel (AFB_clientCtx *client)
{
    int idx;
    int status;

    if (client == NULL)
	return AFB_FAIL;

    pthread_mutex_lock(&sessions.mutex);

    for (idx=0; idx < sessions.max; idx++) {
        if (sessions.store[idx] && (0 == strcmp (client->uuid, sessions.store[idx]->uuid))) break;
    }

    if (idx == sessions.max)
	status = AFB_FAIL;
    else {
        sessions.count--;
        ctxUuidFreeCB (sessions.store[idx]);
        sessions.store[idx]=NULL;
        status = AFB_SUCCESS;
    }

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
        if (NULL == sessions.store[idx]) break;
    }

    if (idx == sessions.max) status=AFB_FAIL;
    else {
        status=AFB_SUCCESS;
        sessions.count ++;
        sessions.store[idx]= client;
    }

    pthread_mutex_unlock(&sessions.mutex);
    return status;
}

// Check if context timeout or not
static int ctxStoreToOld (AFB_clientCtx *ctx, int timeout)
{
    int res;
    time_t now =  time(NULL);
    res = (ctx->timeStamp + timeout) <= now;
    return res;
}

// Loop on every entry and remove old context sessions.hash
void ctxStoreGarbage (const int timeout)
{
    AFB_clientCtx *ctx;
    long idx;

    // Loop on Sessions Table and remove anything that is older than timeout
    for (idx=0; idx < sessions.max; idx++) {
        ctx=sessions.store[idx];
        if ((ctx != NULL) && (ctxStoreToOld(ctx, timeout))) {
            ctxStoreDel (ctx);
        }
    }
}

// This function will return exiting client context or newly created client context
AFB_clientCtx *ctxClientGet (AFB_request *request, int idx)
{
  AFB_clientCtx *clientCtx=NULL;
  const char *uuid;
  uuid_t newuuid;

    if (request->config->token == NULL) return NULL;

    // Check if client as a context or not inside the URL
    uuid  = MHD_lookup_connection_value(request->connection, MHD_GET_ARGUMENT_KIND, key_uuid);

    // if UUID in query we're restfull with no cookies otherwise check for cookie
    if (uuid != NULL)
        request->restfull = TRUE;
    else {
        char cookie[64];
        request->restfull = FALSE;
        snprintf(cookie, sizeof cookie, "%s-%d", COOKIE_NAME, request->config->httpdPort);
        uuid = MHD_lookup_connection_value (request->connection, MHD_COOKIE_KIND, cookie);
    };

    // Warning when no cookie defined MHD_lookup_connection_value may return something !!!
    if ((uuid != NULL) && (strnlen (uuid, 10) >= 10))   {
        // search if client context exist and it not timeout let's use it
        clientCtx = ctxStoreSearch (uuid);

	if (clientCtx) {
            if (ctxStoreToOld (clientCtx, request->config->cntxTimeout)) {
                 // this session is too old let's delete it
                ctxStoreDel (clientCtx);
                clientCtx = NULL;
            } else {
                request->context=clientCtx->contexts[idx];
                request->handle  = clientCtx->plugins[idx]->handle;
                request->uuid= uuid;
                return clientCtx;
            }
        }
    }

    // we have no session let's create one otherwise let's clean any exiting values
    if (clientCtx == NULL) {
        clientCtx = calloc(1, sizeof(AFB_clientCtx)); // init NULL clientContext
        clientCtx->contexts = calloc (1, (unsigned)request->config->pluginCount * (sizeof (void*)));
        clientCtx->plugins  = request->plugins;
    }

    uuid_generate(newuuid);         // create a new UUID
    uuid_unparse_lower(newuuid, clientCtx->uuid);

    // if table is full at 50% let's clean it up
    if(sessions.count > (sessions.max / 2)) ctxStoreGarbage(request->config->cntxTimeout);

    // finally add uuid into hashtable
    if (AFB_SUCCESS != ctxStoreAdd (clientCtx)) {
        free (clientCtx);
        return NULL;
    }

    // if (verbose) fprintf (stderr, "ctxClientGet New uuid=[%s] token=[%s] timestamp=%d\n", clientCtx->uuid, clientCtx->token, clientCtx->timeStamp);
    request->context = clientCtx->contexts[idx];
    request->handle  = clientCtx->plugins[idx]->handle;
    request->uuid=clientCtx->uuid;
    return clientCtx;
}

// Sample Generic Ping Debug API
AFB_error ctxTokenCheck (AFB_clientCtx *clientCtx, AFB_request *request)
{
    const char *token;

    if (clientCtx->contexts == NULL)
	return AFB_EMPTY;

    // this time have to extract token from query list
    token = MHD_lookup_connection_value(request->connection, MHD_GET_ARGUMENT_KIND, key_token);

    // if not token is providing we refuse the exchange
    if ((token == NULL) || (clientCtx->token == NULL))
	return AFB_FALSE;

    // compare current token with previous one
    if ((0 == strcmp (token, clientCtx->token)) && (!ctxStoreToOld (clientCtx, request->config->cntxTimeout))) {
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
        token = MHD_lookup_connection_value(request->connection, MHD_GET_ARGUMENT_KIND, key_token);
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
    clientCtx->timeStamp=time(NULL);

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
    clientCtx->timeStamp=time(NULL);

    return AFB_SUCCESS;
}























#if defined(ALLOWS_SESSION_FILES)

#define AFB_SESSION_JTYPE "AFB_session"
#define AFB_SESSION_JLIST "AFB_sessions.hash"
#define AFB_SESSION_JINFO "AFB_infos"


#define AFB_CURRENT_SESSION "active-session"  // file link name within sndcard dir
#define AFB_DEFAULT_SESSION "current-session" // should be in sync with UI

// let's return only sessions.hash files
static int fileSelect (const struct dirent *entry) {
   return (strstr (entry->d_name, ".afb") != NULL);
}

static  json_object *checkCardDirExit (AFB_session *session, AFB_request *request ) {
    int  sessionDir, cardDir;

    // card name should be more than 3 character long !!!!
    if (strlen (request->prefix) < 3) {
       return (jsonNewMessage (AFB_FAIL,"Fail invalid plugin=%s", request->prefix));
    }

    // open session directory
    sessionDir = open (session->config->sessiondir, O_DIRECTORY);
    if (sessionDir < 0) {
          return (jsonNewMessage (AFB_FAIL,"Fail to open directory [%s] error=%s", session->config->sessiondir, strerror(sessionDir)));
    }

   // create session sndcard directory if it does not exit
    cardDir = openat (sessionDir, request->prefix,  O_DIRECTORY);
    if (cardDir < 0) {
          cardDir  = mkdirat (sessionDir, request->prefix, O_RDWR | S_IRWXU | S_IRGRP);
          if (cardDir < 0) {
              return (jsonNewMessage (AFB_FAIL,"Fail to create directory [%s/%s] error=%s", session->config->sessiondir, request->prefix, strerror(cardDir)));
          }
    }
    close (sessionDir);
    return NULL;
}

// Create a link toward last used sessionname within sndcard directory
static void makeSessionLink (const char *cardname, const char *sessionname) {
   char linkname [256], filename [256];
   int err;
   // create a link to keep track of last uploaded sessionname for this card
   strncpy (filename, sessionname, sizeof(filename));
   strncat (filename, ".afb", sizeof(filename));

   strncpy (linkname, cardname, sizeof(linkname));
   strncat (linkname, "/", sizeof(filename));
   strncat (linkname, AFB_CURRENT_SESSION, sizeof(linkname));
   strncat (linkname, ".afb", sizeof(filename));
   unlink (linkname); // remove previous link if any
   err = symlink (filename, linkname);
   if (err < 0) fprintf (stderr, "Fail to create link %s->%s error=%s\n", linkname, filename, strerror(errno));
}

// verify we can read/write in session dir
AFB_error sessionCheckdir (AFB_session *session) {

   int err;

   // in case session dir would not exist create one
   if (verbose) fprintf (stderr, "AFB:notice checking session dir [%s]\n", session->config->sessiondir);
   mkdir(session->config->sessiondir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

   // change for session directory
   err = chdir(session->config->sessiondir);
   if (err) {
     fprintf(stderr,"AFB: Fail to chdir to %s error=%s\n", session->config->sessiondir, strerror(err));
     return err;
   }

   // verify we can write session in directory
   json_object *dummy= json_object_new_object();
   json_object_object_add (dummy, "checked"  , json_object_new_int (getppid()));
   err = json_object_to_file ("./AFB-probe.json", dummy);
   if (err < 0) return err;

   return AFB_SUCCESS;
}

// create a session in current directory
json_object *sessionList (AFB_session *session, AFB_request *request) {
    json_object *sessionsJ, *ajgResponse;
    struct stat fstat;
    struct dirent **namelist;
    int  count, sessionDir;

    // if directory for card's sessions.hash does not exist create it
    ajgResponse = checkCardDirExit (session, request);
    if (ajgResponse != NULL) return ajgResponse;

    // open session directory
    sessionDir = open (session->config->sessiondir, O_DIRECTORY);
    if (sessionDir < 0) {
          return (jsonNewMessage (AFB_FAIL,"Fail to open directory [%s] error=%s", session->config->sessiondir, strerror(sessionDir)));
    }

    count = scandirat (sessionDir, request->prefix, &namelist, fileSelect, alphasort);
    close (sessionDir);

    if (count < 0) {
        return (jsonNewMessage (AFB_FAIL,"Fail to scan sessions.hash directory [%s/%s] error=%s", session->config->sessiondir, request->prefix, strerror(sessionDir)));
    }
    if (count == 0) return (jsonNewMessage (AFB_EMPTY,"[%s] no session at [%s]", request->prefix, session->config->sessiondir));

    // loop on each session file, retrieve its date and push it into json response object
    sessionsJ = json_object_new_array();
    while (count--) {
         json_object *sessioninfo;
         char timestamp [64];
         char *filename;

         // extract file name and last modification date
         filename = namelist[count]->d_name;
         printf("%s\n", filename);
         stat(filename,&fstat);
         strftime (timestamp, sizeof(timestamp), "%c", localtime (&fstat.st_mtime));
         filename[strlen(filename)-4] = '\0'; // remove .afb extension from filename

         // create an object by session with last update date
         sessioninfo = json_object_new_object();
         json_object_object_add (sessioninfo, "date" , json_object_new_string (timestamp));
         json_object_object_add (sessioninfo, "session" , json_object_new_string (filename));
         json_object_array_add (sessionsJ, sessioninfo);

         free(namelist[count]);
    }

    // free scandir structure
    free(namelist);

    // everything is OK let's build final response
    ajgResponse = json_object_new_object();
    json_object_object_add (ajgResponse, "jtype" , json_object_new_string (AFB_SESSION_JLIST));
    json_object_object_add (ajgResponse, "status"  , jsonNewStatus(AFB_SUCCESS));
    json_object_object_add (ajgResponse, "data"    , sessionsJ);

    return (ajgResponse);
}

// Load Json session object from disk
json_object *sessionFromDisk (AFB_session *session, AFB_request *request, char *name) {
    json_object *jsonSession, *jtype, *response;
    const char *ajglabel;
    char filename [256];
    int defsession;

    if (name == NULL) {
        return  (jsonNewMessage (AFB_FATAL,"session name missing &session=MySessionName"));
    }

    // check for current session request
    defsession = (strcmp (name, AFB_DEFAULT_SESSION) ==0);

    // if directory for card's sessions.hash does not exist create it
    response = checkCardDirExit (session, request);
    if (response != NULL) return response;

    // add name and file extension to session name
    strncpy (filename, request->prefix, sizeof(filename));
    strncat (filename, "/", sizeof(filename));
    if (defsession) strncat (filename, AFB_CURRENT_SESSION, sizeof(filename)-1);
    else strncat (filename, name, sizeof(filename)-1);
    strncat (filename, ".afb", sizeof(filename));

    // just upload json object and return without any further processing
    jsonSession = json_object_from_file (filename);

    if (jsonSession == NULL)  return (jsonNewMessage (AFB_EMPTY,"File [%s] not found", filename));

    // verify that file is a JSON ALSA session type
    if (!json_object_object_get_ex (jsonSession, "jtype", &jtype)) {
        json_object_put   (jsonSession);
        return  (jsonNewMessage (AFB_EMPTY,"File [%s] 'jtype' descriptor not found", filename));
    }

    // check type value is AFB_SESSION_JTYPE
    ajglabel = json_object_get_string (jtype);
    if (strcmp (AFB_SESSION_JTYPE, ajglabel)) {
       json_object_put   (jsonSession);
       return  (jsonNewMessage (AFB_FATAL,"File [%s] jtype=[%s] != [%s]", filename, ajglabel, AFB_SESSION_JTYPE));
    }

    // create a link to keep track of last uploaded session for this card
    if (!defsession) makeSessionLink (request->prefix, name);

    return (jsonSession);
}

// push Json session object to disk
json_object * sessionToDisk (AFB_session *session, AFB_request *request, char *name, json_object *jsonSession) {
   char filename [256];
   time_t rawtime;
   struct tm * timeinfo;
   int err, defsession;
   static json_object *response;

   // we should have a session name
   if (name == NULL) return (jsonNewMessage (AFB_FATAL,"session name missing &session=MySessionName"));

   // check for current session request
   defsession = (strcmp (name, AFB_DEFAULT_SESSION) ==0);

   // if directory for card's sessions.hash does not exist create it
   response = checkCardDirExit (session, request);
   if (response != NULL) return response;

   // add cardname and file extension to session name
   strncpy (filename, request->prefix, sizeof(filename));
   strncat (filename, "/", sizeof(filename));
   if (defsession) strncat (filename, AFB_CURRENT_SESSION, sizeof(filename)-1);
   else strncat (filename, name, sizeof(filename)-1);
   strncat (filename, ".afb", sizeof(filename)-1);


   json_object_object_add(jsonSession, "jtype", json_object_new_string (AFB_SESSION_JTYPE));

   // add a timestamp and store session on disk
   time ( &rawtime );  timeinfo = localtime ( &rawtime );
   // A copy of the string is made and the memory is managed by the json_object
   json_object_object_add (jsonSession, "timestamp", json_object_new_string (asctime (timeinfo)));


   // do we have extra session info ?
   if (request->post->type == AFB_POST_JSON) {
       static json_object *info, *jtype;
       const char  *ajglabel;

       // extract session info from args
       info = json_tokener_parse (request->post->data);
       if (!info) {
            response = jsonNewMessage (AFB_FATAL,"sndcard=%s session=%s invalid json args=%s", request->prefix, name, request->post);
            goto OnErrorExit;
       }

       // info is a valid AFB_info type
       if (!json_object_object_get_ex (info, "jtype", &jtype)) {
            response = jsonNewMessage (AFB_EMPTY,"sndcard=%s session=%s No 'AFB_pluginT' args=%s", request->prefix, name, request->post);
            goto OnErrorExit;
       }

       // check type value is AFB_INFO_JTYPE
       ajglabel = json_object_get_string (jtype);
       if (strcmp (AFB_SESSION_JINFO, ajglabel)) {
              json_object_put   (info); // release info json object
              response = jsonNewMessage (AFB_FATAL,"File [%s] jtype=[%s] != [%s] data=%s", filename, ajglabel, AFB_SESSION_JTYPE, request->post);
              goto OnErrorExit;
       }

       // this is valid info data for our session
       json_object_object_add (jsonSession, "info", info);
   }

   // Finally save session on disk
   err = json_object_to_file (filename, jsonSession);
   if (err < 0) {
        response = jsonNewMessage (AFB_FATAL,"Fail save session = [%s] to disk", filename);
        goto OnErrorExit;
   }


   // create a link to keep track of last uploaded session for this card
   if (!defsession) makeSessionLink (request->prefix, name);

   // we're donne let's return status message
   response = jsonNewMessage (AFB_SUCCESS,"Session= [%s] saved on disk", filename);
   json_object_put (jsonSession);
   return (response);

OnErrorExit:
   json_object_put (jsonSession);
   return response;
}
#endif

