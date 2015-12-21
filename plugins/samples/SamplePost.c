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



// Upload a file and execute a function when upload is done
STATIC json_object* UploadAppli (AFB_request *request, AFB_PostItem *item) {
    
    char *destination = "applications";

    // This is called after PostForm and then after DonePostForm
    if (item == NULL) {
        AFB_PostCtx *postFileCtx = getPostContext(request);      
        if (postFileCtx != NULL) {
            
            // request Application Framework to install application
            
            request->errcode = MHD_HTTP_OK;   // or error is something went wrong;   
            request->jresp   = jsonNewMessage(AFB_FAIL,"UploadFile Post Request file=[%s] done", postFileCtx->path);
        }
    }
    
    // upload multi iteration logic is handle by getPostedFile
    return (getPostFile (request, item, destination));
}

// Simples Upload case just upload a file
STATIC json_object* UploadMusic (AFB_request *request, AFB_PostItem *item) {
    
    char *destination = "musics";

    // upload multi iteration logic is handle by getPostedFile
    return (getPostFile (request, item, destination));
}

// PostForm callback is called multiple times (one or each key within form, or once per file buffer)
// When file has been fully uploaded call is call with item==NULL 
STATIC json_object* UploadImage (AFB_request *request, AFB_PostItem *item) {
    
    // note if directory is relative it will be prefixed by request->config->sessiondir
    char *destination = "images";

    // This is called after PostForm and then after DonePostForm
    if (item == NULL) {
        AFB_PostCtx *postFileCtx = getPostContext(request);
        
        // if postFileCtx == NULL then an error happen [getPostedFile automatically reports errors]
        if (postFileCtx != NULL) {
            // Do something with your newly upload filepath=postFileCtx->path
            request->errcode = MHD_HTTP_OK;     
            request->jresp   = jsonNewMessage(AFB_FAIL,"UploadFile Post Request file=[%s] done", postFileCtx->path);    
            
            // Note: should not return here in order getPostedFile to clear Post resources.
        }
    }
    
    // upload multi iteration logic is handle by getPostedFile
    return (getPostFile (request, item, destination));
}


// NOTE: this sample does not use session to keep test a basic as possible
//       in real application upload-xxx should be protected with AFB_SESSION_CHECK
STATIC  AFB_restapi pluginApis[]= {
  {"ping"         , AFB_SESSION_NONE  , (AFB_apiCB)getPingTest    ,"Ping Rest Test Service"},
  {"upload-json"  , AFB_SESSION_NONE  , (AFB_apiCB)GetJsonByPost  ,"Demo for Json Buffer on Post"},
  {"upload-image" , AFB_SESSION_NONE  , (AFB_apiCB)UploadImage    ,"Demo for file upload"},
  {"upload-music" , AFB_SESSION_NONE  , (AFB_apiCB)UploadMusic    ,"Demo for file upload"},
  {"upload-appli" , AFB_SESSION_NONE  , (AFB_apiCB)UploadAppli    ,"Demo for file upload"},
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
