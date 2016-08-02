/*
 * Copyright (C) 2015, 2016 "IoT.bzh"
 * Author "Fulup Ar Foll"
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <json-c/json.h>

#include <afb/afb-binding.h>

// Dummy sample of Client Application Context
typedef struct {
  int  something;
  void *whateveryouwant;
} MyClientApplicationHandle;


// This function is call when Client Session Context is removed
// Note: when freeCtxCB==NULL standard free/malloc is called
static void clientContextFree(void *context) {
    fprintf (stderr,"Plugin[token] Closing Session\n");
    free (context);
}

// Request Creation of new context if it does not exist
static void clientContextConnect (struct afb_req request)
{
    json_object *jresp;

    // add an application specific client context to session
    afb_req_context_set(request, malloc (sizeof (MyClientApplicationHandle)), clientContextFree);

    // do something intelligent to check if we should or not update level of assurance from 0(anonymous) to 1(logged)
    afb_req_session_set_LOA(request, 1);

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
static void clientContextLogout (struct afb_req request) {
    json_object *jresp;

    /* after this call token will be reset
     *  - no further access to API will be possible
     *  - every context from any used plugin will be freed
     */

    jresp = json_object_new_object();
    json_object_object_add(jresp, "info", json_object_new_string ("Token and all resources are released"));

    // WARNING: if you free context resource manually here do not forget to set *request.context=NULL;
    afb_req_success(request, jresp, NULL);

    afb_req_session_set_LOA(request, 0);
}
// Close and Free context
static void clientGetPing (struct afb_req request) {
    static int count=0;
    json_object *jresp;

    jresp = json_object_new_object();
    json_object_object_add(jresp, "count", json_object_new_int (count ++));

    afb_req_success(request, jresp, NULL);
}


static const struct afb_verb_desc_v1 verbs[]= {
  {"ping"    , AFB_SESSION_NONE                        , clientGetPing       ,"Ping Rest Test Service"},
  {"connect" , AFB_SESSION_LOA_EQ_0 | AFB_SESSION_RENEW, clientContextConnect,"Connect/Login Client"},
  {"refresh" , AFB_SESSION_LOA_GE_1 | AFB_SESSION_RENEW, clientContextRefresh,"Refresh Client Authentication Token"},
  {"check"   , AFB_SESSION_LOA_GE_1                    , clientContextCheck  ,"Check Client Authentication Token"},
  {"logout"  , AFB_SESSION_LOA_GE_1 | AFB_SESSION_CLOSE, clientContextLogout ,"Logout Client and Free resources"},
  {NULL}
};

static const struct afb_binding plugin_desc = {
	.type = AFB_BINDING_VERSION_1,
	.v1 = {
		.info = "Application Framework Binder Authentication sample",
		.prefix = "auth",
		.verbs = verbs
	}
};

const struct afb_binding *afbBindingV1Register (const struct afb_binding_interface *itf)
{
	return &plugin_desc;
}
