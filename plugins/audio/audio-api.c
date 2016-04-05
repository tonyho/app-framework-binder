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

#define _GNU_SOURCE
#include <stdlib.h>

#include "audio-api.h"
#include "audio-alsa.h"

#include "afb-plugin.h"
#include "afb-req-itf.h"

/* ------ BACKEND FUNCTIONS ------- */

void _backend_init (const char *name, audioCtxHandleT *ctx) {

    char *backend_env = getenv ("AFB_AUDIO_OUTPUT");
    unsigned char res = 0;

# ifdef HAVE_PULSE
    if (!backend_env || (strcasecmp (backend_env, "Pulse") == 0))
        res = _pulse_init (name, ctx);
    if (!res)
#endif
    res = _alsa_init (name, ctx);

    if (!res && verbose)
        fprintf (stderr, "Could not initialize Audio backend\n");
}

void _backend_free (audioCtxHandleT *ctx) {

# ifdef HAVE_PULSE
    if (ctx->audio_dev) _pulse_free (ctx); else
# endif
    _alsa_free (ctx->name);
}

void _backend_play (audioCtxHandleT *ctx) {

# ifdef HAVE_PULSE
    if (ctx->audio_dev) _pulse_play (ctx); else
# endif
    _alsa_play (ctx->idx);
}

void _backend_stop (audioCtxHandleT *ctx) {

# ifdef HAVE_PULSE
    if (ctx->audio_dev) _pulse_stop (ctx); else
# endif
    _alsa_stop (ctx->idx);
}

unsigned int _backend_get_volume (audioCtxHandleT *ctx, unsigned int channel) {

# ifdef HAVE_PULSE
    if (ctx->audio_dev) return _pulse_get_volume (ctx, channel); else
# endif
    return _alsa_get_volume (ctx->idx, channel);
}

void _backend_set_volume (audioCtxHandleT *ctx, unsigned int channel, unsigned int vol) {

# ifdef HAVE_PULSE
    if (ctx->audio_dev) _pulse_set_volume (ctx, channel, vol); else
# endif
    _alsa_set_volume (ctx->idx, channel, vol);
}

void _backend_set_volume_all (audioCtxHandleT *ctx, unsigned int vol) {

# ifdef HAVE_PULSE
    if (ctx->audio_dev) _pulse_set_volume_all (ctx, vol); else
# endif
    _alsa_set_volume_all (ctx->idx, vol);
}

unsigned char _backend_get_mute (audioCtxHandleT *ctx) {

# ifdef HAVE_PULSE
    if (ctx->audio_dev) return _pulse_get_mute (ctx); else
# endif
    return _alsa_get_mute (ctx->idx);
}

void _backend_set_mute (audioCtxHandleT *ctx, unsigned char mute) {

# ifdef HAVE_PULSE
    if (ctx->audio_dev) _pulse_set_mute (ctx, mute); else
# endif
    _alsa_set_mute (ctx->idx, mute);
}

void _backend_set_channels (audioCtxHandleT *ctx, unsigned int channels) {

# ifdef HAVE_PULSE
    if (ctx->audio_dev) return; else
# endif
    _alsa_set_channels (ctx->idx, channels);
}

/* ------ LOCAL HELPER FUNCTIONS --------- */

/* private client context creation ; default values */
STATIC audioCtxHandleT* initAudioCtx () {

    audioCtxHandleT *ctx;
    int i;

    ctx = malloc (sizeof(audioCtxHandleT));
    ctx->audio_dev = NULL;
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
    _backend_free (ctx);

    /* clean client context */
    ctx->idx = -1;

    return AFB_SUCCESS;
}

/* called when client session dies [e.g. client quits for more than 15mns] */
STATIC void freeAudio (void *context) {
    free (context);    
}


/* ------ PUBLIC PLUGIN FUNCTIONS --------- */

STATIC void init (struct afb_req request) {        /* AFB_SESSION_CHECK */

    json_object *jresp;

    /* create a private client context */
    if (!request.context)
        request.context = initAudioCtx();

    _backend_init("default", request.context);

    jresp = json_object_new_object();
    json_object_object_add (jresp, "info", json_object_new_string ("Audio initialized"));

    afb_req_success (request, jresp, "Audio initiliazed");
}

STATIC void volume (struct afb_req request) {      /* AFB_SESSION_CHECK */

    audioCtxHandleT *ctx = (audioCtxHandleT*)request.context;
    const char *value = afb_req_argument (request, "value");
    json_object *jresp;
    unsigned int volume[8], i;
    char *volume_i;
    char volume_str[256];
    size_t len_str = 0;

    /* no "?value=" parameter : return current state */
    if (!value) {
        for (i = 0; i < 8; i++) {
            ctx->volume[i] = _backend_get_volume (ctx, i);
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
        volume[0] = (unsigned int) atoi (volume_i);

        if (100 < volume[0]) {
            free (volume_i);
            //request.errcode = MHD_HTTP_SERVICE_UNAVAILABLE;
            afb_req_fail (request, "Failed", "Volume must be between 0 and 100");
            return;
        }
        ctx->volume[0] = volume[0];
        _backend_set_volume (ctx, 0, ctx->volume[0]);
        snprintf (volume_str, sizeof(volume_str), "%d,", ctx->volume[0]);

        for (i = 1; i < 8; i++) {
            volume_i = strtok (NULL, ",");
            /* if there is only one value, set all channels to this one */
            if (!volume_i && i == 1)
               _backend_set_volume_all (ctx, ctx->volume[0]);
            if (!volume_i || 100 < atoi(volume_i) || atoi(volume_i) < 0) {
               ctx->volume[i] = _backend_get_volume (ctx, i);
            } else {
               ctx->volume[i] = (unsigned int) atoi(volume_i);
               _backend_set_volume (ctx, i, ctx->volume[i]);
            }
            len_str = strlen(volume_str);
            snprintf (volume_str+len_str, sizeof(volume_str)-len_str, "%d,", ctx->volume[i]);
        }
        jresp = json_object_new_object();
        json_object_object_add (jresp, "volume", json_object_new_string(volume_str));
    }

    afb_req_success (request, jresp, "Audio - Volume changed");
}

STATIC void channels (struct afb_req request) {    /* AFB_SESSION_CHECK */

    audioCtxHandleT *ctx = (audioCtxHandleT*)request.context;
    const char *value = afb_req_argument (request, "value");
    json_object *jresp = json_object_new_object();
    char channels_str[256];

    /* no "?value=" parameter : return current state */
    if (!value) {
        snprintf (channels_str, sizeof(channels_str), "%d", ctx->channels);
        json_object_object_add (jresp, "channels", json_object_new_string (channels_str));
    }

    /* "?value=" parameter, set channels */
    else {
        ctx->channels = (unsigned int) atoi (value);
        _backend_set_channels (ctx, ctx->channels);

        snprintf (channels_str, sizeof(channels_str), "%d", ctx->channels);
        json_object_object_add (jresp, "channels", json_object_new_string (channels_str));
    }

    afb_req_success (request, jresp, "Audio - Channels set");
}

STATIC void mute (struct afb_req request) {        /* AFB_SESSION_CHECK */

    audioCtxHandleT *ctx = (audioCtxHandleT*)request.context;
    const char *value = afb_req_argument (request, "value");
    json_object *jresp = json_object_new_object();

    /* no "?value=" parameter : return current state */
    if (!value) {
        ctx->mute = _backend_get_mute (ctx);
        ctx->mute ?
            json_object_object_add (jresp, "mute", json_object_new_string ("on"))
          : json_object_object_add (jresp, "mute", json_object_new_string ("off"));
    }

    /* "?value=" parameter is "1" or "true" */
    else if ( atoi(value) == 1 || !strcasecmp(value, "true") ) {
        ctx->mute = 1;
        _backend_set_mute (ctx, ctx->mute);

        json_object_object_add (jresp, "mute", json_object_new_string ("on"));
    }

    /* "?value=" parameter is "0" or "false" */
    else if ( atoi(value) == 0 || !strcasecmp(value, "false") ) {
        ctx->mute = 0;
        _backend_set_mute (ctx, ctx->mute);

        json_object_object_add (jresp, "mute", json_object_new_string ("off"));
    }

    afb_req_success (request, jresp, "Audio - Mute set");
}

STATIC void play (struct afb_req request) {        /* AFB_SESSION_CHECK */

    audioCtxHandleT *ctx = (audioCtxHandleT*)request.context;
    const char *value = afb_req_argument (request, "value");
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
        _backend_play (ctx);

        json_object_object_add (jresp, "play", json_object_new_string ("on"));
    }

    /* "?value=" parameter is "0" or "false" */
    else if ( atoi(value) == 0 || !strcasecmp(value, "false") ) {
        ctx->is_playing = 0;
        _backend_stop (ctx);

        json_object_object_add (jresp, "play", json_object_new_string ("off"));
    }

    afb_req_success (request, jresp, "Audio - Play");
}

STATIC void ping (struct afb_req request) {         /* AFB_SESSION_NONE */
    afb_req_success (request, NULL, "Audio - Ping success");
}

STATIC const struct AFB_restapi pluginApis[]= {
  {"init"    , AFB_SESSION_CHECK,  init      , "Audio API - init"},
  {"volume"  , AFB_SESSION_CHECK,  volume    , "Audio API - volume"},
  {"channels", AFB_SESSION_CHECK,  channels  , "Audio API - channels"},
  {"mute"    , AFB_SESSION_CHECK,  mute      , "Audio API - mute"},
  {"play"    , AFB_SESSION_CHECK,  play      , "Audio API - play"},
  {"ping"    , AFB_SESSION_NONE,   ping      , "Audio API - ping"},
  {NULL}
};

STATIC const struct AFB_plugin plug_desc = {
    .type   = AFB_PLUGIN_JSON,
    .info   = "Application Framework Binder - Audio plugin",
    .prefix = "audio",
    .apis   = pluginApis
};
