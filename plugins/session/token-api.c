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

    // add an application specific client context to session
    request->context = malloc (sizeof (MyClientApplicationHandle));
    
    // Send response to UI
    jresp = json_object_new_object();               
    json_object_object_add(jresp, "token", json_object_new_string ("A New Token and Session Context Was Created"));

    return (jresp);
}

// Before entering here token will be check and renew
STATIC json_object* clientContextRefresh (AFB_request *request) {
    json_object *jresp;

  
    jresp = json_object_new_object();
    json_object_object_add(jresp, "token", json_object_new_string ("Token was refreshed"));              
    
    return (jresp);
}


// Session token will we verified before entering here
STATIC json_object* clientContextCheck (AFB_request *request) {
    
    json_object *jresp = json_object_new_object();    
    json_object_object_add(jresp, "isvalid", json_object_new_boolean (TRUE));       
        
    return (jresp); 
}


// Close and Free context
STATIC json_object* clientContextReset (AFB_request *request) {
    json_object *jresp;
   
    /* after this call token will be reset
     *  - no further access to API will be possible 
     *  - every context from any used plugin will be freed
     */
    
    jresp = json_object_new_object();
    json_object_object_add(jresp, "info", json_object_new_string ("Token and all resources are released"));
    
    // WARNING: if you free context resource manually here do not forget to set request->context=NULL; 
    return (jresp); 
}


// This function is call when Client Session Context is removed
// Note: when freeCtxCB==NULL standard free/malloc is called
STATIC void clientContextFree(void *context, char* uuid) {
    fprintf (stderr,"Plugin[token] Closing Session uuid=[%s]\n", uuid);
    free (context);
}

STATIC  AFB_restapi pluginApis[]= {
  {"ping"    , AFB_SESSION_NONE  , (AFB_apiCB)getPingTest         ,"Ping Rest Test Service"},
  {"create"  , AFB_SESSION_CREATE, (AFB_apiCB)clientContextCreate ,"Request Client Context Creation"},
  {"refresh" , AFB_SESSION_RENEW , (AFB_apiCB)clientContextRefresh,"Refresh Client Context Token"},
  {"check"   , AFB_SESSION_CHECK , (AFB_apiCB)clientContextCheck  ,"Check Client Context Token"},
  {"reset"   , AFB_SESSION_CLOSE , (AFB_apiCB)clientContextReset  ,"Close Client Context and Free resources"},
  {NULL}
};

PUBLIC AFB_plugin *pluginRegister () {
    AFB_plugin *plugin = malloc (sizeof (AFB_plugin));
    plugin->type  = AFB_PLUGIN_JSON; 
    plugin->info  = "Application Framework Binder Service";
    plugin->prefix= "token";  // url base
    plugin->apis  = pluginApis;
    plugin->handle= (void*) "What ever you want";
    plugin->freeCtxCB= (void*) clientContextFree;
    
    return (plugin);
};
