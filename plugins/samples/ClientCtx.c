/*
 * Copyright (C) 2015 "IoT.bzh"
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

#include "afb-plugin.h"

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

// NOTE: this sample does not use session to keep test a basic as possible
//       in real application most APIs should be protected with AFB_SESSION_CHECK
static const struct AFB_restapi pluginApis[]= {
  {"create", AFB_SESSION_CREATE, myCreate  , "Create a new session"},
  {"action", AFB_SESSION_CHECK , myAction  , "Use Session Context"},
  {"close" , AFB_SESSION_CLOSE , myClose   , "Free Context"},
  {NULL}
};

static const struct AFB_plugin plugin_desc = {
	.type = AFB_PLUGIN_JSON,
	.info = "Sample of Client Context Usage",
	.prefix = "context",
	.apis = pluginApis,
};

const struct AFB_plugin *pluginRegister (const struct AFB_interface *itf)
{
	return &plugin_desc;
}

