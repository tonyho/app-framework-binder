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

// Dummy sample of Client Application Context
typedef struct {
  int  something;       
  void *whateveryouwant;
} MyClientApplicationHandle;


// Request Creation of new context if it does not exist
STATIC json_object* clientContextCreate (AFB_request *request) {
    json_object *jresp;
    int   res;
    char *token;
    AFB_clientCtx *client=request->client; // get client context from request
   
    // check we do not already have a session
    if ((client != NULL) && (client->ctx != NULL)) {
        request->errcode=MHD_HTTP_FORBIDDEN;
        return (jsonNewMessage(AFB_FAIL, "Token exist use refresh"));
    }
        
    // request a new client context token and check result 
    if (AFB_UNAUTH == ctxTokenCreate (request)) {
        request->errcode=MHD_HTTP_UNAUTHORIZED;
        jresp= jsonNewMessage(AFB_FAIL, "No/Invalid initial token provided [should match --token=xxxx]");
        return (jresp);
    }
    
    // request a new client context token and check result 
    if (AFB_SUCCESS != ctxTokenCreate (request)) {
        request->errcode=MHD_HTTP_UNAUTHORIZED;
        jresp= jsonNewMessage(AFB_FAIL, "Token Session Not Activated [restart with --token=xxxx]");
        return (jresp);
    }
   
    // add a client context to session
    client->ctx = malloc (sizeof (MyClientApplicationHandle));
    
    // Send response to UI
    jresp = json_object_new_object();               
    json_object_object_add(jresp, "token", json_object_new_string (client->token));

    return (jresp);
}

// Renew an existing context
STATIC json_object* clientContextRefresh (AFB_request *request) {
    json_object *jresp;

    // note: we do not need to parse the old token as clientContextRefresh doit for us
    if (AFB_SUCCESS != ctxTokenRefresh (request)) {
        request->errcode=MHD_HTTP_UNAUTHORIZED;
        jresp= jsonNewMessage(AFB_FAIL, "Token Exchange Broken Refresh Refused");
    } else {
        jresp = json_object_new_object();
        json_object_object_add(jresp, "token", json_object_new_string (request->client->token));              
    }
            
    return (jresp);
}


// Verify a context is still valid 
STATIC json_object* clientContextCheck (AFB_request *request) {
    
    json_object *jresp = json_object_new_object();
    
    // add an error code to respond
    if (AFB_SUCCESS != ctxTokenCheck (request)) {
        request->errcode=MHD_HTTP_UNAUTHORIZED;
        json_object_object_add(jresp, "isvalid", json_object_new_boolean (FALSE));
    } else {
        json_object_object_add(jresp, "isvalid", json_object_new_boolean (TRUE));       
    }
        
    return (jresp); 
}

// Close and Free context
STATIC json_object* clientContextReset (AFB_request *request) {
    json_object *jresp;
   
    // note: we do not need to parse the old token as clientContextRefresh doit for us
    if (AFB_SUCCESS != ctxTokenReset (request)) {
        request->errcode=MHD_HTTP_UNAUTHORIZED;
        jresp= jsonNewMessage(AFB_FAIL, "No Token Client Context [use --token=xxx]");
    } else {
        jresp = json_object_new_object();
        json_object_object_add(jresp, "uuid", json_object_new_string (request->client->uuid));              
    }
    
    return (jresp); 
}

// This function is call when Client Session Context is removed
// Note: when freeCtxCB==NULL standard free/malloc is called
STATIC void clientContextFree(AFB_clientCtx *client) {
    fprintf (stderr,"Plugin[%s] Closing Session uuid=[%s]\n", client->plugin->prefix, client->uuid);
    free (client->ctx);
}

STATIC  AFB_restapi pluginApis[]= {
  {"ping"          , (AFB_apiCB)apiPingTest         ,"Ping Rest Test Service"},
  {"token-create"  , (AFB_apiCB)clientContextCreate ,"Request Client Context Creation"},
  {"token-refresh" , (AFB_apiCB)clientContextRefresh,"Refresh Client Context Token"},
  {"token-check"   , (AFB_apiCB)clientContextCheck  ,"Check Client Context Token"},
  {"token-reset"   , (AFB_apiCB)clientContextReset  ,"Close Client Context and Free resources"},
  {NULL}
};

PUBLIC AFB_plugin *afsvRegister () {
    AFB_plugin *plugin = malloc (sizeof (AFB_plugin));
    plugin->type  = AFB_PLUGIN_JSON; 
    plugin->info  = "Application Framework Binder Service";
    plugin->prefix= "afbs";  // url base
    plugin->apis  = pluginApis;
    plugin->handle= (void*) "What ever you want";
    plugin->freeCtxCB= (void*) clientContextFree;
    
    return (plugin);
};