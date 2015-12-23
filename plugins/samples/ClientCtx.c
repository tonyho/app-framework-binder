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

typedef struct {
  /* 
   * In case your plugin is implemented on multiple files or used share routines
   * with other plugins, it might not be possible to use global static variable.
   * In this case you can attach a static handle to your plugin. This handle
   * is passed within each API call under request->handle
   * 
   */  
  void *anythingYouWant;

  
} MyPluginHandleT;

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
STATIC json_object* myCreate (AFB_request *request) {
    json_object *jresp;
    
    MyClientContextT *ctx= malloc (sizeof (MyClientContextT));
    MyPluginHandleT  *handle = (MyPluginHandleT*) request->handle;

    // store something in our plugin private client context
    ctx->count = 0;
    ctx->abcd  = "SomeThingUseful";        

    request->context = ctx;
    jresp =  jsonNewMessage(AFB_SUCCESS, "SUCCESS: create client context for plugin [%s]", handle->anythingYouWant);
           
    return jresp;
}

// This function can only be called with a valid token. Token should be renew before
// session timeout a standard renew api is avaliable at /api/token/renew this API
// can be called automatically with <token-renew> HTML5 widget.
// ex: http://localhost:1234/api/context/action?token=xxxxxx-xxxxxx-xxxxx-xxxxx-xxxxxx
STATIC json_object* myAction (AFB_request *request) {
    json_object* jresp;
    MyPluginHandleT  *handle = (MyPluginHandleT*) request->handle;
    MyClientContextT *ctx= (MyClientContextT*) request->context;
    
    // store something in our plugin private client context
    ctx->count++;
    jresp =  jsonNewMessage(AFB_SUCCESS, "SUCCESS: plugin [%s] Check=[%d]\n", handle->anythingYouWant, ctx->count);
         
    return jresp;
}

// After execution of this function, client session will be close and if they
// created a context [request->context != NULL] every plugins will be notified
// that they should free context resources.
// ex: http://localhost:1234/api/context/close?token=xxxxxx-xxxxxx-xxxxx-xxxxx-xxxxxx
STATIC json_object* myClose (AFB_request *request) {
    json_object* jresp;
    MyPluginHandleT  *handle = (MyPluginHandleT*) request->handle;
    MyClientContextT *ctx= (MyClientContextT*) request->context;
    
    // store something in our plugin private client context
    ctx->count++;
    jresp =  jsonNewMessage(AFB_SUCCESS, "SUCCESS: plugin [%s] Close=[%d]\n", handle->anythingYouWant, ctx->count);
    
    // Note Context resource should be free in FreeCtxCB and not here in case session timeout.
    return jresp;
}

STATIC void freeCtxCB (MyClientContextT *ctx, MyPluginHandleT *handle, char *uuid) {
    fprintf (stderr, "FreeCtxCB uuid=[%s] Plugin=[%s]  count=[%d]", uuid, handle->anythingYouWant, ctx->count);
    free (ctx);
    
    // Note: handle should be free it is a static resource attached to plugin and not to session
}

// NOTE: this sample does not use session to keep test a basic as possible
//       in real application most APIs should be protected with AFB_SESSION_CHECK
STATIC  AFB_restapi pluginApis[]= {
  {"create", AFB_SESSION_CREATE, (AFB_apiCB)myCreate  , "Create a new session"},
  {"action", AFB_SESSION_CHECK , (AFB_apiCB)myAction   , "Use Session Context"},
  {"close" , AFB_SESSION_CLOSE , (AFB_apiCB)myClose   , "Free Context"},
  {NULL}
};

PUBLIC AFB_plugin *pluginRegister () {
    
    // Plugin handle should not be in stack (malloc or static)
    STATIC MyPluginHandleT handle;
    
    AFB_plugin *plugin = malloc (sizeof (AFB_plugin));
    plugin->type     = AFB_PLUGIN_JSON;
    plugin->info     = "Sample of Client Context Usage";
    plugin->prefix   = "context";
    plugin->apis     = pluginApis;
    plugin->handle   = &handle;
    plugin->freeCtxCB= (AFB_freeCtxCB) freeCtxCB;
    
    // feed plugin handle before returning from registration
    handle.anythingYouWant = "My Plugin Handle";
    
    return (plugin);
};
