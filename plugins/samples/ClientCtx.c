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

#include <afb/afb-plugin.h>

typedef struct {
  /* 
   * client context is attached a session but private to a each plugin.
   * Context is passed to each API under request->context
   * 
   * Note:
   *  -client context is free when a session is closed. Developer should not
   *   forget that even if context is private to each plugin, session is unique
   *   to a client. When session close, every plugin are notified to free there
   *   private context.
   *  -by default standard "free" function from libc is used to free context.
   *   Developer may define it own under plugin->freeCB. This call received
   *   FreeCtxCb(void *ClientCtx, void*PluginHandle, char*SessionUUID) if
   *   FreeCtxCb=(void*)-1 then context wont be free by session manager.
   *  -when an API use AFB_SESSION_RESET this close the session and each plugin
   *   will be notified to free ressources.
   */
    
  int  count;
  char *abcd;
  
} MyClientContextT;

// This function is call at session open time. Any client trying to 
// call it with an already open session will be denied.
// Ex: http://localhost:1234/api/context/create?token=123456789
static void myCreate (struct afb_req request)
{
    MyClientContextT *ctx = malloc (sizeof (MyClientContextT));

    // store something in our plugin private client context
    ctx->count = 0;
    ctx->abcd  = "SomeThingUseful";        

    afb_req_context_set(request, ctx, free);
    afb_req_success_f(request, NULL, "SUCCESS: create client context for plugin [%s]", ctx->abcd);
}

// This function can only be called with a valid token. Token should be renew before
// session timeout a standard renew api is avaliable at /api/token/renew this API
// can be called automatically with <token-renew> HTML5 widget.
// ex: http://localhost:1234/api/context/action?token=xxxxxx-xxxxxx-xxxxx-xxxxx-xxxxxx
static void myAction (struct afb_req request)
{
    MyClientContextT *ctx = (MyClientContextT*) afb_req_context_get(request);
    
    // store something in our plugin private client context
    ctx->count++;
    afb_req_success_f(request, NULL, "SUCCESS: plugin [%s] Check=[%d]\n", ctx->abcd, ctx->count);
}

// After execution of this function, client session will be close and if they
// created a context [request->context != NULL] every plugins will be notified
// that they should free context resources.
// ex: http://localhost:1234/api/context/close?token=xxxxxx-xxxxxx-xxxxx-xxxxx-xxxxxx
static void myClose (struct afb_req request)
{
    MyClientContextT *ctx = (MyClientContextT*) afb_req_context_get(request);
    
    // store something in our plugin private client context
    ctx->count++;
    afb_req_success_f(request, NULL, "SUCCESS: plugin [%s] Close=[%d]\n", ctx->abcd, ctx->count);
}

// Set the LOA
static void setLOA(struct afb_req request, unsigned loa)
{
    if (afb_req_session_set_LOA(request, loa))
	afb_req_success_f(request, NULL, "loa set to %u", loa);
    else
	afb_req_fail_f(request, "failed", "can't set loa to %u", loa);
}

static void clientSetLOA0(struct afb_req request)
{
    setLOA(request, 0);
}

static void clientSetLOA1(struct afb_req request)
{
    setLOA(request, 1);
}

static void clientSetLOA2(struct afb_req request)
{
    setLOA(request, 2);
}

static void clientSetLOA3(struct afb_req request)
{
    setLOA(request, 3);
}

static void clientCheckLOA(struct afb_req request)
{
    afb_req_success(request, NULL, "LOA checked and okay");
}

// NOTE: this sample does not use session to keep test a basic as possible
//       in real application most APIs should be protected with AFB_SESSION_CHECK
static const struct AFB_verb_desc_v1 verbs[]= {
  {"create", AFB_SESSION_CREATE, myCreate  , "Create a new session"},
  {"action", AFB_SESSION_CHECK , myAction  , "Use Session Context"},
  {"close" , AFB_SESSION_CLOSE , myClose   , "Free Context"},
  {"set_loa_0", AFB_SESSION_RENEW, clientSetLOA0       ,"Set level of assurance to 0"},
  {"set_loa_1", AFB_SESSION_RENEW, clientSetLOA1       ,"Set level of assurance to 1"},
  {"set_loa_2", AFB_SESSION_RENEW, clientSetLOA2       ,"Set level of assurance to 2"},
  {"set_loa_3", AFB_SESSION_RENEW, clientSetLOA3       ,"Set level of assurance to 3"},
  {"check_loa_ge_0", AFB_SESSION_LOA_GE_0, clientCheckLOA ,"Check whether level of assurance is greater or equal to 0"},
  {"check_loa_ge_1", AFB_SESSION_LOA_GE_1, clientCheckLOA ,"Check whether level of assurance is greater or equal to 1"},
  {"check_loa_ge_2", AFB_SESSION_LOA_GE_2, clientCheckLOA ,"Check whether level of assurance is greater or equal to 2"},
  {"check_loa_ge_3", AFB_SESSION_LOA_GE_3, clientCheckLOA ,"Check whether level of assurance is greater or equal to 3"},
  {"check_loa_le_0", AFB_SESSION_LOA_LE_0, clientCheckLOA ,"Check whether level of assurance is lesser or equal to 0"},
  {"check_loa_le_1", AFB_SESSION_LOA_LE_1, clientCheckLOA ,"Check whether level of assurance is lesser or equal to 1"},
  {"check_loa_le_2", AFB_SESSION_LOA_LE_2, clientCheckLOA ,"Check whether level of assurance is lesser or equal to 2"},
  {"check_loa_le_3", AFB_SESSION_LOA_LE_3, clientCheckLOA ,"Check whether level of assurance is lesser or equal to 3"},
  {"check_loa_eq_0", AFB_SESSION_LOA_EQ_0, clientCheckLOA ,"Check whether level of assurance is equal to 0"},
  {"check_loa_eq_1", AFB_SESSION_LOA_EQ_1, clientCheckLOA ,"Check whether level of assurance is equal to 1"},
  {"check_loa_eq_2", AFB_SESSION_LOA_EQ_2, clientCheckLOA ,"Check whether level of assurance is equal to 2"},
  {"check_loa_eq_3", AFB_SESSION_LOA_EQ_3, clientCheckLOA ,"Check whether level of assurance is equal to 3"},
  {NULL}
};

static const struct AFB_plugin plugin_desc = {
	.type = AFB_PLUGIN_VERSION_1,
	.v1 = {
		.info = "Sample of Client Context Usage",
		.prefix = "context",
		.verbs = verbs,
	}
};

const struct AFB_plugin *pluginAfbV1Register (const struct AFB_interface *itf)
{
	return &plugin_desc;
}

