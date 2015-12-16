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

#include "audio-api.h"
#include "audio-alsa.h"


/* ------ LOCAL HELPER FUNCTIONS --------- */

/* private client context creation ; default values */
STATIC audioCtxHandleT* initAudioCtx () {

    audioCtxHandleT *ctx;

    ctx = malloc (sizeof(audioCtxHandleT));
    ctx->volume = 25;
    ctx->rate = 22050;
    ctx->channels = 2;

    return ctx;
}

/* called when client session dies [e.g. client quits for more than 15mns] */
STATIC json_object* freeAudio (AFB_clientCtx *client) {

    //releaseAudio (client->plugin->handle, client->ctx);
    free (client->ctx);
    
    return jsonNewMessage (AFB_SUCCESS, "Released radio and client context");
}


/* ------ PUBLIC PLUGIN FUNCTIONS --------- */

STATIC json_object* init (AFB_request *request) {       /* AFB_SESSION_CREATE */

    audioCtxHandleT *ctx;
    json_object *jresp;

    /* create a private client context */
    ctx = initAudioCtx();
    request->client->ctx = (audioCtxHandleT*)ctx;
    
    _alsa_init("default", ctx);
    
    jresp = json_object_new_object();
    json_object_object_add (jresp, "token", json_object_new_string (request->client->token));
}


STATIC AFB_restapi pluginApis[]= {
  {"init"   , AFB_SESSION_CREATE, (AFB_apiCB)init       , "Audio API - init"},
//  {"error"  , AFB_SESSION_CHECK,   (AFB_apiCB)wrongApi   , "Ping Application Framework"},

  {NULL}
};

PUBLIC AFB_plugin *audioRegister () {
    AFB_plugin *plugin = malloc (sizeof(AFB_plugin));
    plugin->type   = AFB_PLUGIN_JSON;
    plugin->info   = "Application Framework Binder - Audio plugin";
    plugin->prefix = "audio";        
    plugin->apis   = pluginApis;

    plugin->freeCtxCB = freeAudio;

    return (plugin);
};