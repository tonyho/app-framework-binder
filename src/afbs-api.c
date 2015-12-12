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
PUBLIC json_object* clientContextCreate (AFB_request *request) {
    json_object *jresp;
    int   res;
    char *token;
    AFB_clientCtx *client=request->client; // get client context from request

    // check we do not already have a session
    if (client->handle != NULL) {
        request->errcode=MHD_HTTP_FORBIDDEN;
        return (jsonNewMessage(AFB_FAIL, "Token exist use refresh"));
    }
        
    // request a new client context token and check result
    ctxTokenCreate (request);
   
    // add a client handle to session
    client->handle = malloc (sizeof (MyClientApplicationHandle));
    
    // Send response to UI
    jresp = json_object_new_object();               
    json_object_object_add(jresp, "token", json_object_new_string (client->token));

    return (jresp);
}

// Renew an existing context
PUBLIC json_object* clientContextRefresh (AFB_request *request) {
    json_object *jresp;

    // check we do not already have a session
    if (request->client == NULL) return (jsonNewMessage(AFB_FAIL, "No Previous Token use Create"));
    
    // note: we do not need to parse the old token as clientContextRefresh doit for us
    if (ctxTokenRefresh (request)) {
        jresp = json_object_new_object();
        json_object_object_add(jresp, "token", json_object_new_string (request->client->token));              
    } else {
        request->errcode=MHD_HTTP_UNAUTHORIZED;
        jresp= jsonNewMessage(AFB_FAIL, "Token Exchange Broken Refresh Refused");
    }
            
    return (jresp);
}


// Verify a context is still valid 
PUBLIC json_object* clientContextCheck (AFB_request *request) {
    json_object *jresp;
    int isvalid;

    // check is token is valid
    isvalid= ctxTokenCheck (request);
    
    // add an error code to respond
    if (!isvalid) request->errcode=MHD_HTTP_UNAUTHORIZED;
    
    // prepare response for client side application
    jresp = json_object_new_object();
    json_object_object_add(jresp, "isvalid", json_object_new_boolean (isvalid));
    
    return (jresp); 
}

// Close and Free context
PUBLIC json_object* clientContextReset (AFB_request *request) {
    json_object *jresp;
    
    jresp = json_object_new_object();
    json_object_object_add(jresp, "done", json_object_new_boolean (ctxTokenReset (request)));
    
    return (jresp); 
}


STATIC  AFB_restapi pluginApis[]= {
  {"ping"          , (AFB_apiCB)apiPingTest         ,"Ping Rest Test Service", NULL},
  {"token-create"  , (AFB_apiCB)clientContextCreate ,"Request Client Context Creation",NULL},
  {"token-refresh" , (AFB_apiCB)clientContextRefresh,"Refresh Client Context Token",NULL},
  {"token-check"   , (AFB_apiCB)clientContextCheck  ,"Check Client Context Token",NULL},
  {"token-reset"   , (AFB_apiCB)clientContextReset  ,"Close Client Context and Free resources",NULL},
  {0,0,0,0}
};

PUBLIC AFB_plugin *afsvRegister () {
    AFB_plugin *plugin = malloc (sizeof (AFB_plugin));
    plugin->type  = AFB_PLUGIN; 
    plugin->info  = "Application Framework Binder Service";
    plugin->prefix= "afbs";  // url base
    plugin->apis  = pluginApis;
    
    return (plugin);
};