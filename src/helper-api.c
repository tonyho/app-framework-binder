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

#define _GNU_SOURCE

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/*
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
*/

#include "local-def.h"
#include "afb-req-itf.h"

// handle to hold queryAll values
typedef struct {
     char    *msg;
     size_t  idx;
     size_t  len;
} queryHandleT;

// Error code are requested through function to manage json usage count
typedef struct {
  int   level;
  const char* label;
  json_object *json;
} AFB_errorT;

static AFB_errorT   AFBerr [AFB_UNAUTH+1];
static json_object *jTypeStatic;

PUBLIC int verbose;

static const char *ERROR_LABEL[] = {"false", "true", "fatal", "fail", "warning", "empty", "success", "done", "unauth"};



// Helper to retrieve argument from  connection
const char* getQueryValue(const AFB_request * request, const char *name) {
    return afb_req_argument(*request->areq, name);
}

static int getQueryCB (queryHandleT *query, struct afb_arg arg) {
    if (query->idx >= query->len)
	return 0;
    query->idx += (unsigned)snprintf (&query->msg[query->idx], query->len-query->idx, " %s: %s\'%s\',", arg.name, arg.is_file?"FILE=":"", arg.value);
    return 1; /* continue to iterate */
}

// Helper to retrieve argument from  connection
size_t getQueryAll(AFB_request * request, char *buffer, size_t len) {
    queryHandleT query;
    buffer[0] = '\0'; // start with an empty string
    query.msg = buffer;
    query.len = len;
    query.idx = 0;

    afb_req_iterate(*request->areq, (void*)getQueryCB, &query);
    buffer[len-1] = 0;
    return query.idx >= len ? len - 1 : query.idx;
}

#if 0
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

#endif

static void jsoninit()
{
  int idx, verbosesav;

  if (jTypeStatic)
	return;

  // initialise JSON constant messages and increase reference count to make them permanent
  verbosesav = verbose;
  verbose = 0;  // run initialisation in silent mode
  jTypeStatic = json_object_new_string ("AFB_message");
  for (idx = 0; idx <= AFB_UNAUTH; idx++) {
     AFBerr[idx].level = idx;
     AFBerr[idx].label = ERROR_LABEL [idx];
     AFBerr[idx].json  = jsonNewMessage (idx, NULL);
  }
  verbose = verbosesav;
}


// build an ERROR message and return it as a valid json object
json_object *json_add_status (json_object *obj, const char *status, const char *info)
{
	if (obj == NULL)
		obj = json_object_new_object();
	json_object_object_add(obj, "status", json_object_new_string(status));
	if (info)
		json_object_object_add(obj, "info", json_object_new_string(info));
	return obj;
}

// build an ERROR message and return it as a valid json object
json_object *json_add_status_v (json_object *obj, const char *status, const char *info, va_list args)
{
	char *message;
	if (info == NULL || vasprintf(&message, info, args) < 0)
		message = NULL;
	obj = json_add_status(obj, status, message);
	free(message);
	return obj;
}


// build an ERROR message and return it as a valid json object
json_object *json_add_status_f (json_object *obj, const char *status, const char *info, ...)
{
	va_list args;
	va_start(args, info);
	obj = json_add_status_v(obj, status, info, args);
	va_end(args);
	return obj;
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
   json_object_object_add (AFBResponse, "jtype", json_object_get (jTypeStatic));
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

#if 0
{
  jtype: "AFB_message"
  request:
    {
      prefix: "",
      api: "",
      status: "", /* exist, fail, empty, null, processed */
      info: "",
      uuid: "",
      token: "",
      timeout: ""
    }
  response: ...
}
#endif

