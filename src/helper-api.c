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


// handle to hold queryAll values
typedef struct {
     char    *msg;
     int     idx;
     size_t  len;
} queryHandleT;

// Sample Generic Ping Debug API
PUBLIC json_object* getPingTest(AFB_request *request) {
    static pingcount = 0;
    json_object *response;
    char query  [256];
    char session[256];

    int len;
    AFB_clientCtx *client=request->client; // get client context from request
    
    // request all query key/value
    len = getQueryAll (request, query, sizeof(query));
    if (len == 0) strncpy (query, "NoSearchQueryList", sizeof(query));
    
    // check if we have some post data
    if (request->post == NULL)  request->post->data="NoData"; 
    
    // check is we have a session and a plugin handle
    if (client == NULL) strncpy (session,"NoSession", sizeof(session));       
    else snprintf(session, sizeof(session),"uuid=%s token=%s ctx=0x%x handle=0x%x", client->uuid, client->token, client->ctx, client->ctx); 
        
    // return response to caller
    response = jsonNewMessage(AFB_SUCCESS, "Ping Binder Daemon count=%d CtxtId=%d query={%s} session={%s} PostData: [%s] "
               , pingcount++, request->client->cid, query, session, request->post->data);
    return (response);
}


// Helper to retrieve argument from  connection
PUBLIC const char* getQueryValue(AFB_request * request, char *name) {
    const char *value;

    value = MHD_lookup_connection_value(request->connection, MHD_GET_ARGUMENT_KIND, name);
    return (value);
}

STATIC int getQueryCB (void*handle, enum MHD_ValueKind kind, const char *key, const char *value) {
    queryHandleT *query = (queryHandleT*)handle;
        
    query->idx += snprintf (&query->msg[query->idx],query->len," %s: \'%s\',", key, value);
}

// Helper to retrieve argument from  connection
PUBLIC int getQueryAll(AFB_request * request, char *buffer, size_t len) {
    queryHandleT query;
    buffer[0] = '\0'; // start with an empty string
    query.msg= buffer;
    query.len= len;
    query.idx= 0;

    MHD_get_connection_values (request->connection, MHD_GET_ARGUMENT_KIND, getQueryCB, &query);
    return (len);
}

// Helper to retrieve POST handle
PUBLIC AFB_PostHandle* getPostHandle (AFB_request *request) {
    if (request->post == NULL) return (NULL);
    return ((AFB_PostHandle*) request->post->data);
}

// Helper to retrieve POST file context
PUBLIC AFB_PostCtx* getPostContext (AFB_request *request) {
    AFB_PostHandle* postHandle;
    if (request->post == NULL) return (NULL);
    
    postHandle = (AFB_PostHandle*) request->post->data;
    if (postHandle == NULL) return NULL;
       
    return ((AFB_PostCtx*) postHandle->ctx);
}

PUBLIC json_object* getPostFile (AFB_request *request, AFB_PostItem *item, char* destination) {

    AFB_PostHandle *postHandle = getPostHandle(request);
    AFB_PostCtx *appCtx;
    char filepath[512];
    int len;
            
    // This is called after PostForm and then after DonePostForm
    if (item == NULL) {
        json_object* jresp;
        appCtx = (AFB_PostCtx*) postHandle->ctx;
        
        // No Post Application Context [something really bad happen]
        if (appCtx == NULL) {
            request->errcode = MHD_HTTP_EXPECTATION_FAILED;
            return(jsonNewMessage(AFB_FAIL,"Error: PostForm no PostContext to free\n"));          
        }
        
        // We have a context but last Xform iteration fail.
        if (appCtx->jerror != NULL) {
            // request->errcode = appCtx->errcode;
            jresp = appCtx->jerror;  // retrieve previous error from postCtx
        } else jresp = jsonNewMessage(AFB_FAIL,"UploadFile Post Request file=[%s] done", appCtx->path);
        
        // Error or not let's free all resources
        close(appCtx->fd);
        free (appCtx->path);
        free (appCtx);
        return (jresp);  
    }
    
    // Make sure it's a valid PostForm request
    if (!request->post && request->post->type != AFB_POST_FORM) {
        appCtx->jerror= jsonNewMessage(AFB_FAIL,"This is not a valid PostForm request\n");
        goto ExitOnError;
    } 
    
    // Check this is a file element
    if (item->filename == NULL) {
        appCtx->jerror= jsonNewMessage(AFB_FAIL,"No Filename attached to key=%s\n", item->key);
        goto ExitOnError;
    }
    
    // Check we got something in buffer
    if (item->len <= 0) {       
        appCtx->jerror= jsonNewMessage(AFB_FAIL,"Buffer size NULL key=%s]\n", item->key);
        goto ExitOnError;
    }

    // Extract Application Context from posthandle [NULL == 1st iteration]    
    appCtx = (AFB_PostCtx*) postHandle->ctx;

    // This is the 1st Item iteration let's open output file and allocate necessary resources
    if (appCtx == NULL)  {
        int destDir;
        
        // Create an application specific context
        appCtx = calloc (1, sizeof(AFB_PostCtx)); // May place anything here until post->completeCB handle resources liberation
        appCtx->path = strdup (filepath);
        
        // attach application to postHandle
        postHandle->ctx = (void*) appCtx;   // May place anything here until post->completeCB handle resources liberation  
        
        // Build destination directory full path
        if (destination[0] != '/') {
           strncpy (filepath, request->config->sessiondir, sizeof(filepath)); 
           strncat (filepath, destination, sizeof(filepath)); 
           strncat (filepath, "/", sizeof(filepath));
           strncat (filepath, destination, sizeof(filepath)); 
        } else strncpy (filepath, destination, sizeof(filepath));
        

        // make sure destination directory exist
        destDir = openat (filepath, request->plugin,  O_DIRECTORY);
        if (destDir < 0) {
          destDir = mkdir(filepath,O_RDWR | S_IRWXU | S_IRGRP); 
          if (destDir < 0) {
            appCtx->jerror= jsonNewMessage(AFB_FAIL,"Fail to Create destination directory=[%s] error=%s\n", filepath, strerror(errno));
            goto ExitOnError;
          }
        } else close (destDir);
        
        strncat (filepath, "/", sizeof(filepath));
        strncat (filepath, item->filename, sizeof(filepath));  

        if((appCtx->fd = open(filepath, O_RDWR |O_CREAT, S_IRWXU|S_IRGRP)) < 0) {
            appCtx->jerror= jsonNewMessage(AFB_FAIL,"Fail to Create destination=[%s] error=%s\n", filepath, strerror(errno));
            goto ExitOnError;
        } 
    } else {     
        // reuse existing application context
        appCtx = (AFB_PostCtx*) postHandle->ctx;  
    } 

    // Check we successfully wrote full buffer
    len = write (appCtx->fd, item->data, item->len);
    if (item->len != len) {
        appCtx->jerror= jsonNewMessage(AFB_FAIL,"Fail to write file [%s] at [%s] error=\n", item->filename, strerror(errno));
        goto ExitOnError;
    }
  
    // every intermediary iteration should return Success & NULL
    request->errcode = MHD_HTTP_OK;
    return NULL;
    
ExitOnError:    
    request->errcode = MHD_HTTP_EXPECTATION_FAILED;
    return NULL;
}
