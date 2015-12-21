/*
 * Copyright (C) 2015 "IoT.bzh"
 * Author "Manuel Bachmann"
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
    ctx->idx = -1;
    ctx->volume = 25;
    ctx->channels = 2;
    ctx->mute = 0;
    ctx->is_playing = 0;

    return ctx;
}

STATIC AFB_error releaseAudio (audioCtxHandleT *ctx) {

    /* power it off */
    _alsa_free (ctx->idx);

    /* clean client context */
    ctx->idx = -1;

    return AFB_SUCCESS;
}

/* called when client session dies [e.g. client quits for more than 15mns] */
STATIC json_object* freeAudio (AFB_clientCtx *client) {

    releaseAudio (client->ctx);
    free (client->ctx);
    
    return jsonNewMessage (AFB_SUCCESS, "Released radio and client context");
}


/* ------ PUBLIC PLUGIN FUNCTIONS --------- */

STATIC json_object* init (AFB_request *request) {       /* AFB_SESSION_CREATE */

    audioCtxHandleT *ctx;
    json_object *jresp;
    int idx;

    /* create a private client context */
    ctx = initAudioCtx();
    request->client->ctx = (audioCtxHandleT*)ctx;
    
    _alsa_init("default", ctx);
    
    jresp = json_object_new_object();
    json_object_object_add (jresp, "token", json_object_new_string (request->client->token));
    return jresp;
}

STATIC json_object* volume (AFB_request *request) {      /* AFB_SESSION_CHECK */

    audioCtxHandleT *ctx = (audioCtxHandleT*)request->client->ctx;
    const char *value = getQueryValue (request, "value");
    json_object *jresp;
    int volume;
    char volume_str[256];

    /* no "?value=" parameter : return current state */
    if (!value) {
        ctx->volume = _alsa_get_volume (ctx->idx);
        snprintf (volume_str, sizeof(volume_str), "%d", ctx->volume);
        jresp = json_object_new_object();
        json_object_object_add (jresp, "volume", json_object_new_string(volume_str));
    }

    /* "?value=" parameter, set volume */
    else {
        volume = atoi (value);
        if (100 < volume < 0) {
            request->errcode = MHD_HTTP_SERVICE_UNAVAILABLE;
            return (jsonNewMessage (AFB_FAIL, "Volume must be between 0 and 100"));
        }
        ctx->volume = volume;
        _alsa_set_volume (ctx->idx, ctx->volume);

        snprintf (volume_str, sizeof(volume_str), "%d", ctx->volume);
        jresp = json_object_new_object();
        json_object_object_add (jresp, "volume", json_object_new_string(volume_str));
    }

    return jresp;
}

STATIC json_object* channels (AFB_request *request) {    /* AFB_SESSION_CHECK */

    audioCtxHandleT *ctx = (audioCtxHandleT*)request->client->ctx;
    const char *value = getQueryValue (request, "value");
    json_object *jresp = json_object_new_object();
    char channels_str[256];

    /* no "?value=" parameter : return current state */
    if (!value) {
        snprintf (channels_str, sizeof(channels_str), "%d", ctx->channels);
        json_object_object_add (jresp, "channels", json_object_new_string (channels_str));
    }

    /* "?value=" parameter, set channels */
    else {
        ctx->channels = atoi (value);
        _alsa_set_channels (ctx->idx, ctx->channels);

        snprintf (channels_str, sizeof(channels_str), "%d", ctx->channels);
        json_object_object_add (jresp, "channels", json_object_new_string (channels_str));
    }

    return jresp;
}

STATIC json_object* mute (AFB_request *request) {        /* AFB_SESSION_CHECK */

    audioCtxHandleT *ctx = (audioCtxHandleT*)request->client->ctx;
    const char *value = getQueryValue (request, "value");
    json_object *jresp = json_object_new_object();

    /* no "?value=" parameter : return current state */
    if (!value) {
        ctx->mute = _alsa_get_mute (ctx->idx);
        ctx->mute ?
            json_object_object_add (jresp, "mute", json_object_new_string ("on"))
          : json_object_object_add (jresp, "mute", json_object_new_string ("off"));
    }

    /* "?value=" parameter is "1" or "true" */
    else if ( atoi(value) == 1 || !strcasecmp(value, "true") ) {
        ctx->mute = 1;
        _alsa_set_mute (ctx->idx, ctx->mute);

        json_object_object_add (jresp, "mute", json_object_new_string ("on"));
    }

    /* "?value=" parameter is "0" or "false" */
    else if ( atoi(value) == 0 || !strcasecmp(value, "false") ) {
        ctx->mute = 0;
        _alsa_set_mute (ctx->idx, ctx->mute);

        json_object_object_add (jresp, "mute", json_object_new_string ("off"));
    }

    return jresp;
}

STATIC json_object* play (AFB_request *request) {        /* AFB_SESSION_CHECK */

    audioCtxHandleT *ctx = (audioCtxHandleT*)request->client->ctx;
    const char *value = getQueryValue (request, "value");
    json_object *jresp = json_object_new_object();

    /* no "?value=" parameter : return current state */
    if (!value) {
        ctx->is_playing ?
            json_object_object_add (jresp, "play", json_object_new_string ("on"))
          : json_object_object_add (jresp, "play", json_object_new_string ("off"));
    }

    /* "?value=" parameter is "1" or "true" */
    else if ( atoi(value) == 1 || !strcasecmp(value, "true") ) {
        ctx->is_playing = 1;
        _alsa_play (ctx->idx);

        json_object_object_add (jresp, "play", json_object_new_string ("on"));
    }

    /* "?value=" parameter is "0" or "false" */
    else if ( atoi(value) == 0 || !strcasecmp(value, "false") ) {
        ctx->is_playing = 0;
        _alsa_stop (ctx->idx);

        json_object_object_add (jresp, "play", json_object_new_string ("off"));
    }

    return jresp;
}

STATIC json_object* refresh (AFB_request *request) {     /* AFB_SESSION_RENEW */
    json_object *jresp = json_object_new_object();
    json_object_object_add(jresp, "token", json_object_new_string (request->client->token));
    return jresp;
}

STATIC json_object* ping (AFB_request *request) {         /* AFB_SESSION_NONE */
    return jsonNewMessage(AFB_SUCCESS, "Ping Binder Daemon - Radio API");
}


STATIC AFB_restapi pluginApis[]= {
  {"init"    , AFB_SESSION_CREATE, (AFB_apiCB)init      , "Audio API - init"},
  {"volume"  , AFB_SESSION_CHECK,  (AFB_apiCB)volume    , "Audio API - volume"},
  {"channels", AFB_SESSION_CHECK,  (AFB_apiCB)channels  , "Audio API - channels"},
  {"mute"    , AFB_SESSION_CHECK,  (AFB_apiCB)mute      , "Audio API - mute"},
  {"play"    , AFB_SESSION_CHECK,  (AFB_apiCB)play      , "Audio API - play"},
  {"refresh" , AFB_SESSION_RENEW,  (AFB_apiCB)refresh   , "Audio API - refresh"},
  {"ping"    , AFB_SESSION_NONE,   (AFB_apiCB)ping      , "Audio API - ping"},
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
