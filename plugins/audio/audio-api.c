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
    int i;

    ctx = malloc (sizeof(audioCtxHandleT));
    ctx->idx = -1;
    for (i = 0; i < 8; i++)
        ctx->volume[i] = 25;
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
STATIC void freeAudio (void *context) {
    free (context);    
}


/* ------ PUBLIC PLUGIN FUNCTIONS --------- */

STATIC json_object* init (AFB_request *request) {        /* AFB_SESSION_CHECK */

    json_object *jresp;
    int idx;

    /* create a private client context */
    request->context = initAudioCtx();
    
    _alsa_init("default", request->context);
    
    jresp = json_object_new_object();
    json_object_object_add (jresp, "info", json_object_new_string ("Audio initialised"));
    return (jresp);
}

STATIC json_object* volume (AFB_request *request) {      /* AFB_SESSION_CHECK */

    audioCtxHandleT *ctx = (audioCtxHandleT*)request->context;
    const char *value = getQueryValue (request, "value");
    json_object *jresp;
    int volume[8], i;
    char *volume_i;
    char volume_str[256];
    int len_str = 0;

    /* no "?value=" parameter : return current state */
    if (!value) {
        for (i = 0; i < 8; i++) {
            ctx->volume[i] = _alsa_get_volume (ctx->idx, i);
            snprintf (volume_str+len_str, sizeof(volume_str)-len_str, "%d,", ctx->volume[i]);
            len_str = strlen(volume_str);
        }
        jresp = json_object_new_object();
        json_object_object_add (jresp, "volume", json_object_new_string(volume_str));
    }

    /* "?value=" parameter, set volume */
    else {
        volume_i = strdup (value);
        volume_i = strtok (volume_i, ",");
        volume[0] = atoi (volume_i);

        if (100 < volume[0] < 0) {
            free (volume_i);
            request->errcode = MHD_HTTP_SERVICE_UNAVAILABLE;
            return (jsonNewMessage (AFB_FAIL, "Volume must be between 0 and 100"));
        }
        ctx->volume[0] = volume[0];
        _alsa_set_volume (ctx->idx, 0, ctx->volume[0]);
        snprintf (volume_str, sizeof(volume_str), "%d,", ctx->volume[0]);

        for (i = 1; i < 8; i++) {
            volume_i = strtok (NULL, ",");
            /* if there is only one value, set all channels to this one */
            if (!volume_i && i == 1)
               _alsa_set_volume_all (ctx->idx, ctx->volume[0]);
            if (!volume_i || 100 < atoi(volume_i) < 0) {
               ctx->volume[i] = _alsa_get_volume (ctx->idx, i);
            } else {
               ctx->volume[i] = atoi(volume_i);
               _alsa_set_volume (ctx->idx, i, ctx->volume[i]);
            }
            len_str = strlen(volume_str);
            snprintf (volume_str+len_str, sizeof(volume_str)-len_str, "%d,", ctx->volume[i]);
        }
        jresp = json_object_new_object();
        json_object_object_add (jresp, "volume", json_object_new_string(volume_str));
    }

    return jresp;
}

STATIC json_object* channels (AFB_request *request) {    /* AFB_SESSION_CHECK */

    audioCtxHandleT *ctx = (audioCtxHandleT*)request->context;
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

    audioCtxHandleT *ctx = (audioCtxHandleT*)request->context;
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

    audioCtxHandleT *ctx = (audioCtxHandleT*)request->context;
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

STATIC json_object* ping (AFB_request *request) {         /* AFB_SESSION_NONE */
    return jsonNewMessage(AFB_SUCCESS, "Ping Binder Daemon - Radio API");
}

STATIC AFB_restapi pluginApis[]= {
  {"init"    , AFB_SESSION_CHECK,  (AFB_apiCB)init      , "Audio API - init"},
  {"volume"  , AFB_SESSION_CHECK,  (AFB_apiCB)volume    , "Audio API - volume"},
  {"channels", AFB_SESSION_CHECK,  (AFB_apiCB)channels  , "Audio API - channels"},
  {"mute"    , AFB_SESSION_CHECK,  (AFB_apiCB)mute      , "Audio API - mute"},
  {"play"    , AFB_SESSION_CHECK,  (AFB_apiCB)play      , "Audio API - play"},
  {"ping"    , AFB_SESSION_NONE,   (AFB_apiCB)ping      , "Audio API - ping"},
  {NULL}
};

PUBLIC AFB_plugin *pluginRegister () {
    AFB_plugin *plugin = malloc (sizeof(AFB_plugin));
    plugin->type   = AFB_PLUGIN_JSON;
    plugin->info   = "Application Framework Binder - Audio plugin";
    plugin->prefix = "audio";        
    plugin->apis   = pluginApis;

    plugin->freeCtxCB = (AFB_freeCtxCB)freeAudio;

    return (plugin);
};
