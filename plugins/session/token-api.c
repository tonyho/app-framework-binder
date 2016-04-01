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

#define _GNU_SOURCE
#include <stdio.h>
#include <json.h>

#include "afb-plugin.h"
#include "afb-req-itf.h"

// Dummy sample of Client Application Context
typedef struct {
  int  something;       
  void *whateveryouwant;
} MyClientApplicationHandle;


// Request Creation of new context if it does not exist
static void clientContextCreate (struct afb_req request)
{
    json_object *jresp;

    // add an application specific client context to session
    *request.context = malloc (sizeof (MyClientApplicationHandle));
    
    // Send response to UI
    jresp = json_object_new_object();               
    json_object_object_add(jresp, "token", json_object_new_string ("A New Token and Session Context Was Created"));

    afb_req_success(request, jresp, NULL);
}

// Before entering here token will be check and renew
static void clientContextRefresh (struct afb_req request) {
    json_object *jresp;

  
    jresp = json_object_new_object();
    json_object_object_add(jresp, "token", json_object_new_string ("Token was refreshed"));              
    
    afb_req_success(request, jresp, NULL);
}


// Session token will we verified before entering here
static void clientContextCheck (struct afb_req request) {
    
    json_object *jresp = json_object_new_object();    
    json_object_object_add(jresp, "isvalid", json_object_new_boolean (TRUE));       
        
    afb_req_success(request, jresp, NULL);
}


// Close and Free context
static void clientContextReset (struct afb_req request) {
    json_object *jresp;
   
    /* after this call token will be reset
     *  - no further access to API will be possible 
     *  - every context from any used plugin will be freed
     */
    
    jresp = json_object_new_object();
    json_object_object_add(jresp, "info", json_object_new_string ("Token and all resources are released"));
    
    // WARNING: if you free context resource manually here do not forget to set *request.context=NULL; 
    afb_req_success(request, jresp, NULL);
}
// Close and Free context
static void clientGetPing (struct afb_req request) {
    static int count=0;
    json_object *jresp;

    jresp = json_object_new_object();
    json_object_object_add(jresp, "count", json_object_new_int (count ++));
    
    afb_req_success(request, jresp, NULL);
}


// This function is call when Client Session Context is removed
// Note: when freeCtxCB==NULL standard free/malloc is called
static void clientContextFree(void *context) {
    fprintf (stderr,"Plugin[token] Closing Session\n");
    free (context);
}

static const struct AFB_restapi pluginApis[]= {
  {"ping"    , AFB_SESSION_NONE  , clientGetPing       ,"Ping Rest Test Service"},
  {"create"  , AFB_SESSION_CREATE, clientContextCreate ,"Request Client Context Creation"},
  {"refresh" , AFB_SESSION_RENEW , clientContextRefresh,"Refresh Client Context Token"},
  {"check"   , AFB_SESSION_CHECK , clientContextCheck  ,"Check Client Context Token"},
  {"reset"   , AFB_SESSION_CLOSE , clientContextReset  ,"Close Client Context and Free resources"},
  {NULL}
};

static const struct AFB_plugin plugin_desc = {
	.type = AFB_PLUGIN_JSON,
	.info = "Application Framework Binder Service",
	.prefix = "token",
	.apis = pluginApis,
	.freeCtxCB = clientContextFree
};

const struct AFB_plugin *pluginRegister ()
{
	return &plugin_desc;
}
