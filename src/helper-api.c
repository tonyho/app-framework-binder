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
 */

#include "../include/local-def.h"
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>


// handle to hold queryAll values
typedef struct {
     char    *msg;
     int     idx;
     size_t  len;
} queryHandleT;

static AFB_errorT   AFBerr [AFB_SUCCESS+1];
static json_object *jTypeStatic;
PUBLIC int verbose;

/* ------------------------------------------------------------------------------
 * Get localtime and return in a string
 * ------------------------------------------------------------------------------ */

PUBLIC char * configTime (void) {
  static char reqTime [26];
  time_t tt;
  struct tm *rt;

  /* Get actual Date and Time */
  time (&tt);
  rt = localtime (&tt);

  strftime (reqTime, sizeof (reqTime), "(%d-%b %H:%M)",rt);

  // return pointer on static data
  return (reqTime);
}


// Sample Generic Ping Debug API
json_object* getPingTest(AFB_request *request) {
    static int pingcount = 0;
    json_object *response;
    char query  [256];
    char session[256];
    int len;
    
    // request all query key/value
    len = getQueryAll (request, query, sizeof(query));
    if (len == 0) strncpy (query, "NoSearchQueryList", sizeof(query));
    
    // check if we have some post data
    if (request->post == NULL)  request->post->data="NoData"; 
          
    // return response to caller
    response = jsonNewMessage(AFB_SUCCESS, "Ping Binder Daemon count=%d uuid=%s query={%s} session={0x%x} PostData: [%s] "
               , pingcount++, request->uuid, query, session, request->post->data);
    return (response);
}


// Helper to retrieve argument from  connection
const char* getQueryValue(const AFB_request * request, const char *name) {
    const char *value;

    value = MHD_lookup_connection_value(request->connection, MHD_GET_ARGUMENT_KIND, name);
    return (value);
}

static int getQueryCB (void*handle, enum MHD_ValueKind kind, const char *key, const char *value) {
    queryHandleT *query = (queryHandleT*)handle;
        
    query->idx += snprintf (&query->msg[query->idx],query->len," %s: \'%s\',", key, value);
    return MHD_YES; /* continue to iterate */
}

// Helper to retrieve argument from  connection
int getQueryAll(AFB_request * request, char *buffer, size_t len) {
    queryHandleT query;
    buffer[0] = '\0'; // start with an empty string
    query.msg = buffer;
    query.len = len;
    query.idx = 0;

    MHD_get_connection_values (request->connection, MHD_GET_ARGUMENT_KIND, getQueryCB, &query);
    return (len);
}

// Helper to retrieve POST handle
AFB_PostHandle* getPostHandle (AFB_request *request) {
    if (request->post == NULL) return (NULL);
    return ((AFB_PostHandle*) request->post->data);
}

// Helper to retrieve POST file context
AFB_PostCtx* getPostContext (AFB_request *request) {
    AFB_PostHandle* postHandle;
    if (request->post == NULL) return (NULL);
    
    postHandle = (AFB_PostHandle*) request->post->data;
    if (postHandle == NULL) return NULL;
       
    return ((AFB_PostCtx*) postHandle->ctx);
}

char* getPostPath (AFB_request *request) {
    AFB_PostHandle *postHandle = getPostHandle(request);
    AFB_PostCtx *postFileCtx;
    
    if (postHandle == NULL) return NULL;
    
    postFileCtx = (AFB_PostCtx*) postHandle->ctx;
    if (postFileCtx == NULL) return NULL;
  
    return (postFileCtx->path);
}

json_object* getPostFile (AFB_request *request, AFB_PostItem *item, char* destination) {

    AFB_PostHandle *postHandle = getPostHandle(request);
    AFB_PostCtx *postFileCtx;
    char filepath[512];
    ssize_t len;
            
    // This is called after PostForm and then after DonePostForm
    if (item == NULL) {
        json_object* jresp;
        postFileCtx = (AFB_PostCtx*) postHandle->ctx;
        
        // No Post Application Context [something really bad happen]
        if (postFileCtx == NULL) {
            request->errcode = MHD_HTTP_EXPECTATION_FAILED;
            return(jsonNewMessage(AFB_FAIL,"Error: PostForm no PostContext to free\n"));          
        }
        
        // We have a context but last Xform iteration fail or application set a message
        if (request->jresp != NULL) {
            jresp = request->jresp;  // retrieve previous error from postCtx
        } else jresp = jsonNewMessage(AFB_SUCCESS,"getPostFile Post Request done");
        
        // Error or not let's free all resources
        close(postFileCtx->fd);
        free (postFileCtx->path);
        free (postFileCtx);
        return (jresp);  
    }
#if defined(PLEASE_FIX_ME_THE_ERROR_IS_postFileCtx_NOT_INITIALIZED)
    // Make sure it's a valid PostForm request
    if (!request->post && request->post->type != AFB_POST_FORM) {
        postFileCtx->jresp= jsonNewMessage(AFB_FAIL,"This is not a valid PostForm request\n");
        goto ExitOnError;
    } 
    
    // Check this is a file element
    if (item->filename == NULL) {
        postFileCtx->jresp= jsonNewMessage(AFB_FAIL,"No Filename attached to key=%s\n", item->key);
        goto ExitOnError;
    }
    
    // Check we got something in buffer
    if (item->len <= 0) {       
        postFileCtx->jresp= jsonNewMessage(AFB_FAIL,"Buffer size NULL key=%s]\n", item->key);
        goto ExitOnError;
    }
#endif
    // Extract Application Context from posthandle [NULL == 1st iteration]    
    postFileCtx = (AFB_PostCtx*) postHandle->ctx;

    // This is the 1st Item iteration let's open output file and allocate necessary resources
    if (postFileCtx == NULL)  {
        DIR* destDir;
        
        // Create an application specific context
        postFileCtx = calloc (1, sizeof(AFB_PostCtx)); // May place anything here until post->completeCB handle resources liberation
        
        // attach application to postHandle
        postHandle->ctx = (void*) postFileCtx;   // May place anything here until post->completeCB handle resources liberation  
        
        // Build destination directory full path
        if (destination[0] != '/') {
           strncpy (filepath, request->config->sessiondir, sizeof(filepath)); 
           strncat (filepath, "/", sizeof(filepath));
           strncat (filepath, destination, sizeof(filepath)); 
        } else strncpy (filepath, destination, sizeof(filepath));

        
        // make sure destination directory exist
        destDir = opendir (filepath);
        if (destDir == NULL) {
          if (mkdir(filepath,O_RDWR | S_IRWXU | S_IRGRP) < 0) {
            postFileCtx->jresp= jsonNewMessage(AFB_FAIL,"Fail to Create destination directory=[%s] error=%s\n", filepath, strerror(errno));
            goto ExitOnError;
          }
        } else closedir (destDir);
        
        strncat (filepath, "/", sizeof(filepath));
        strncat (filepath, item->filename, sizeof(filepath));  

        postFileCtx->path = strdup (filepath);       
        if (verbose) fprintf(stderr, "getPostFile path=%s\n", filepath);
       
        if((postFileCtx->fd = open(filepath, O_RDWR |O_CREAT, S_IRWXU|S_IRGRP)) <= 0) {
            postFileCtx->jresp= jsonNewMessage(AFB_FAIL,"Fail to Create destination File=[%s] error=%s\n", filepath, strerror(errno));
            goto ExitOnError;
        } 
    } else {     
        // reuse existing application context
        postFileCtx = (AFB_PostCtx*) postHandle->ctx;  
    } 

    // Check we successfully wrote full buffer
    len = write (postFileCtx->fd, item->data, item->len);
    if ((ssize_t)item->len != len) {
        postFileCtx->jresp= jsonNewMessage(AFB_FAIL,"Fail to write file [%s] at [%s] error=\n", item->filename, strerror(errno));
        goto ExitOnError;
    }
  
    // every intermediary iteration should return Success & NULL
    request->errcode = MHD_HTTP_OK;
    return NULL;
    
ExitOnError:    
    request->errcode = MHD_HTTP_EXPECTATION_FAILED;
    return NULL;
}



static void jsoninit()
{
  int idx, verbosesav;

  if (jTypeStatic)
	return;

  // initialise JSON constant messages and increase reference count to make them permanent
  verbosesav = verbose;
  verbose = 0;  // run initialisation in silent mode
  jTypeStatic = json_object_new_string ("AFB_message");
  for (idx = 0; idx <= AFB_SUCCESS; idx++) {
     AFBerr[idx].level = idx;
     AFBerr[idx].label = ERROR_LABEL [idx];
     AFBerr[idx].json  = jsonNewMessage (idx, NULL);
  }
  verbose = verbosesav;
}



// get JSON object from error level and increase its reference count
struct json_object *jsonNewStatus (AFB_error level)
{
  jsoninit();
  json_object *target =  AFBerr[level].json;
  json_object_get (target);

  return (target);
}

// get AFB object type with adequate usage count
struct json_object *jsonNewjtype (void)
{
  jsoninit();
  json_object_get (jTypeStatic); // increase reference count
  return (jTypeStatic);
}

// build an ERROR message and return it as a valid json object
struct json_object *jsonNewMessage (AFB_error level, char* format, ...) {
   static int count = 0;
   json_object * AFBResponse;
   va_list args;
   char message [512];

  jsoninit();

   // format message
   if (format != NULL) {
       va_start(args, format);
       vsnprintf (message, sizeof (message), format, args);
       va_end(args);
   }

   AFBResponse = json_object_new_object();
   json_object_object_add (AFBResponse, "jtype", jsonNewjtype ());
   json_object_object_add (AFBResponse, "status" , json_object_new_string (ERROR_LABEL[level]));
   if (format != NULL) {
        json_object_object_add (AFBResponse, "info"   , json_object_new_string (message));
   }
   if (verbose) {
        fprintf (stderr, "AFB:%-6s [%3d]: ", AFBerr [level].label, count++);
        if (format != NULL) {
            fprintf (stderr, "%s", message);
        } else {
            fprintf (stderr, "No Message");
        }
        fprintf (stderr, "\n");
   }

   return (AFBResponse);
}

// Dump a message on stderr
void jsonDumpObject (struct json_object * jObject) {

   if (verbose) {
        fprintf (stderr, "AFB:dump [%s]\n", json_object_to_json_string(jObject));
   }
}

