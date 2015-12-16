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
   int fd; 
} appPostCtx;

// This function is call when PostForm processing is completed
STATIC void DonePostForm (AFB_request *request) {
    AFB_PostHandle  *postHandle = (AFB_PostHandle*)request->post->data;
    appPostCtx *appCtx= postHandle->ctx;
    
    // Close upload file ID
    close (appCtx->fd);

    // Free application specific handle
    free (postHandle->ctx);
    
    if (verbose) fprintf (stderr, "DonePostForm upload done\n");
}


// WARNING: PostForm callback are call multiple time (one or each key within form)
// When processing POST_JSON request->data hold a PostHandle and not data directly as for POST_JSON
STATIC json_object* ProcessPostForm (AFB_request *request, AFB_PostItem *item) {

    AFB_PostHandle  *postHandle;
    appPostCtx *appCtx;
    char filepath[512];
            
    // When Post is fully processed the same callback is call with a item==NULL
    if (item == NULL) {
        // Close file, Free handle
        
        request->errcode = MHD_HTTP_OK;
        return(jsonNewMessage(AFB_SUCCESS,"File [%s] uploaded at [%s] error=\n", item->filename, request->config->sessiondir));  
    }
    
    // Let's make sure this is a valid PostForm request
    if (!request->post && request->post->type != AFB_POST_FORM) {
        request->errcode = MHD_HTTP_FORBIDDEN;
        return(jsonNewMessage(AFB_FAIL,"This is not a valid PostForm request\n"));          
    } else {
        // In AFB_POST_FORM case post->data is a PostForm handle
        postHandle = (AFB_PostHandle*) request->post->data;
        appCtx = (appPostCtx*) postHandle->ctx;
    }

    // Check this is a file element
    if (0 != strcmp (item->key, "file")) {
        request->errcode = MHD_HTTP_FORBIDDEN;
        return (jsonNewMessage(AFB_FAIL,"No File within element key=%s\n", item->key));
    }

    // This is the 1st Item iteration let's open output file and allocate necessary resources
    if (postHandle->ctx == NULL)  {
        int fd;
        
        strncpy (filepath, request->config->sessiondir, sizeof(filepath));
        strncat (filepath, "/", sizeof(filepath));
        strncat (filepath, item->filename, sizeof(filepath));  

        if((fd = open(request->config->sessiondir, O_RDONLY)) < 0) {
            request->errcode = MHD_HTTP_FORBIDDEN;
            return (jsonNewMessage(AFB_FAIL,"Fail to Upload file [%s] at [%s] error=\n", item->filename, request->config->sessiondir, strerror(errno)));
        };            

        // Create an application specific context
        appCtx = malloc (sizeof(appPostCtx)); // May place anything here until post->completeCB handle resources liberation
        appCtx->fd = fd;
        
        // attach application to postHandle
        postHandle->ctx = (void*) appCtx;   // May place anything here until post->completeCB handle resources liberation        
        postHandle->completeCB = (AFB_apiCB)DonePostForm; // CallBack when Form Processing is finished
        
    } else {
        // this is not the call, FD is already open
        appCtx = (appPostCtx*) postHandle->ctx;
    }

    // We have something to write
    if (item->len > 0) {
        
        if (!write (appCtx->fd, item->data, item->len)) {
            request->errcode = MHD_HTTP_FORBIDDEN;
            return (jsonNewMessage(AFB_FAIL,"Fail to write file [%s] at [%s] error=\n", item->filename, strerror(errno)));
        }
    }
  
    // every event should return Sucess or Form processing stop
    request->errcode = MHD_HTTP_OK;
    return NULL;
}

// This function is call when Client Session Context is removed
// Note: when freeCtxCB==NULL standard free/malloc is called
STATIC void clientContextFree(AFB_clientCtx *client) {
    fprintf (stderr,"Plugin[%s] Closing Session uuid=[%s]\n", client->plugin->prefix, client->uuid);
    free (client->ctx);
}

STATIC  AFB_restapi pluginApis[]= {
  {"ping"   , AFB_SESSION_NONE  , (AFB_apiCB)apiPingTest         ,"Ping Rest Test Service"},
  {"upload" , AFB_SESSION_NONE  , (AFB_apiCB)ProcessPostForm     ,"Demo for file upload"},
  {NULL}
};

PUBLIC AFB_plugin *afsvRegister () {
    AFB_plugin *plugin = malloc (sizeof (AFB_plugin));
    plugin->type  = AFB_PLUGIN_JSON; 
    plugin->info  = "Application Framework Binder Service";
    plugin->prefix= "post";  // url base
    plugin->apis  = pluginApis;
    plugin->handle= (void*) "What ever you want";
    plugin->freeCtxCB= (void*) clientContextFree;
    
    return (plugin);
};