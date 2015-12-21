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
 */


#include "local-def.h"


// In this case or handle is quite basic
typedef struct {
   int   fd; 
   char *path; 
   json_object* jerror;
} appPostCtx;

// With content-type=json data are directly avaliable in request->post->data
STATIC json_object* GetJsonByPost (AFB_request *request) {
    json_object* jresp;
    char query [256];
    int  len;
    
    // check if we have some post data
    if (request->post == NULL)  request->post->data="NoData"; 
    
    // Get all query string [Note real app should probably use value=getQueryValue(request,"key")]
    len = getQueryAll (request, query, sizeof(query));
    if (len == 0) strncpy (query, "NoSearchQueryList", sizeof(query));
    
    // for debug/test return response to caller
    jresp = jsonNewMessage(AFB_SUCCESS, "GetJsonByPost query={%s} PostData: [%s]", query, request->post->data);
    
    return (jresp);    
}

// This function is call when PostForm processing is completed
STATIC void DonePostForm (AFB_request *request) { 
    
    // Retrieve PostHandle Context from request
    AFB_PostHandle *postHandle = getPostHandle(request);
    appPostCtx *appCtx= (appPostCtx*) postHandle->ctx;

    if (verbose) fprintf (stderr, "DonePostForm file=[%s]upload done\n", appCtx->path);
    

}


// PostForm callback is called multiple times (one or each key within form, or once per file buffer)
// When processing POST_FORM request->data holds a PostHandle and not data directly as for POST_JSON
// When file has been fully uploaded call is call with item==NULL it is application responsibility to free appPostCtx
STATIC json_object* UploadFile (AFB_request *request, AFB_PostItem *item) {

    AFB_PostHandle *postHandle = getPostHandle(request);
    appPostCtx *appCtx;
    char filepath[512];
    int len;
            
    // This is called after PostForm and then after DonePostForm
    if (item == NULL) {
        json_object* jresp;
        appCtx = (appPostCtx*) postHandle->ctx;
        
        // No Post Application Context [something really bad happen]
        if (appCtx == NULL) {
            request->errcode = MHD_HTTP_EXPECTATION_FAILED;
            return(jsonNewMessage(AFB_FAIL,"Error: PostForm no PostContext to free\n"));          
        }
        
        // We have a context but last Xform iteration fail.
        if (appCtx->jerror != NULL) {
            request->errcode = MHD_HTTP_EXPECTATION_FAILED;
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
    appCtx = (appPostCtx*) postHandle->ctx;

    // This is the 1st Item iteration let's open output file and allocate necessary resources
    if (appCtx == NULL)  {
        // Create an application specific context
        appCtx = calloc (1, sizeof(appPostCtx)); // May place anything here until post->completeCB handle resources liberation
        appCtx->path = strdup (filepath);
        
        // attach application to postHandle
        postHandle->ctx = (void*) appCtx;   // May place anything here until post->completeCB handle resources liberation  
        
        // Allocate an application specific handle to this post
        strncpy (filepath, request->config->sessiondir, sizeof(filepath));
        strncat (filepath, "/", sizeof(filepath));
        strncat (filepath, item->filename, sizeof(filepath));  

        if((appCtx->fd = open(filepath, O_RDWR |O_CREAT, S_IRWXU|S_IRGRP)) < 0) {
            appCtx->jerror= jsonNewMessage(AFB_FAIL,"Fail to Create destination=[%s] error=%s\n", filepath, strerror(errno));
            goto ExitOnError;
        } 
    } else {     
        // reuse existing application context
        appCtx = (appPostCtx*) postHandle->ctx;  
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


// NOTE: this sample does not use session to keep test a basic as possible
//       in real application upload-xxx should be protected with AFB_SESSION_CHECK
STATIC  AFB_restapi pluginApis[]= {
  {"ping"         , AFB_SESSION_NONE  , (AFB_apiCB)apiPingTest    ,"Ping Rest Test Service"},
  {"upload-json"  , AFB_SESSION_NONE  , (AFB_apiCB)GetJsonByPost  ,"Demo for Json Buffer on Post"},
  {"upload-image" , AFB_SESSION_NONE  , (AFB_apiCB)UploadFile     ,"Demo for file upload"},
  {"upload-music" , AFB_SESSION_NONE  , (AFB_apiCB)UploadFile     ,"Demo for file upload"},
  {"upload-appli" , AFB_SESSION_NONE  , (AFB_apiCB)UploadFile     ,"Demo for file upload"},
  {NULL}
};

PUBLIC AFB_plugin *pluginRegister () {
    AFB_plugin *plugin = malloc (sizeof (AFB_plugin));
    plugin->type  = AFB_PLUGIN_JSON; 
    plugin->info  = "Application Framework Binder Service";
    plugin->prefix= "post";  // url base
    plugin->apis  = pluginApis;
    plugin->handle= (void*) "What ever you want";
    
    return (plugin);
};
