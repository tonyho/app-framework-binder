/*
 * Copyright (C) 2015 "IoT.bzh"
 * Author "Manuel Bachmann"
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
#include <stdlib.h>
#include <json-c/json.h>

#include "audio-api.h"
#include "audio-alsa.h"
#ifdef HAVE_PULSE
#include "audio-pulse.h"
#endif

#include "afb-plugin.h"
#include "afb-req-itf.h"

/* ------ BACKEND FUNCTIONS ------- */

unsigned char _backend_init (const char *name, audioCtxHandleT *ctx) {

    char *backend_env = getenv ("AFB_AUDIO_OUTPUT");
    unsigned char res = 0;

# ifdef HAVE_PULSE
    if (!backend_env || (strcasecmp (backend_env, "Pulse") == 0))
        res = _pulse_init (name, ctx);
    if (!res)
#endif
    res = _alsa_init (name, ctx);

    if (!res)
        fprintf (stderr, "Could not initialize Audio backend\n");

    return res;
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

/* private client context constructor ; default values */
static audioCtxHandleT* initAudioCtx () {

    audioCtxHandleT *ctx;
    int i;

    ctx = malloc (sizeof(audioCtxHandleT));
    ctx->audio_dev = NULL;
    ctx->name = NULL;
    ctx->idx = -1;
    for (i = 0; i < 8; i++)
        ctx->volume[i] = 25;
    ctx->channels = 2;
    ctx->mute = 0;
    ctx->is_playing = 0;

    return ctx;
}

static void releaseAudioCtx (void *context) {

    audioCtxHandleT *ctx = (audioCtxHandleT*) context;

    /* power it off */
    _backend_free (ctx);

    /* clean client context */
    ctx->audio_dev = NULL;
    if (ctx->name)
		free (ctx->name);
    ctx->idx = -1;
    free (ctx);
}


/* ------ PUBLIC PLUGIN FUNCTIONS --------- */

static void init (struct afb_req request) {        /* AFB_SESSION_CHECK */

    audioCtxHandleT *ctx = (audioCtxHandleT*) afb_req_context_get(request);
    json_object *jresp;

    /* create a private client context */
	if (!ctx) {
        ctx = initAudioCtx();
        afb_req_context_set (request, ctx, releaseAudioCtx);
    }

    if (!_backend_init ("default", ctx)) {
        afb_req_fail (request, "failed", "backend initialization failed");
    }

    jresp = json_object_new_object();
    json_object_object_add (jresp, "init", json_object_new_string ("success"));
    afb_req_success (request, jresp, "Audio initialized");
}

static void volume (struct afb_req request) {      /* AFB_SESSION_CHECK */

    audioCtxHandleT *ctx = (audioCtxHandleT*) afb_req_context_get(request);
    struct afb_arg arg = afb_req_get (request, "value");
    const char *value = arg.value;
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
            afb_req_fail (request, "failed", "volume must be between 0 and 100");
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

static void channels (struct afb_req request) {    /* AFB_SESSION_CHECK */

    audioCtxHandleT *ctx = (audioCtxHandleT*) afb_req_context_get(request);
    struct afb_arg arg = afb_req_get (request, "value");
    const char *value = arg.value;
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

static void mute (struct afb_req request) {        /* AFB_SESSION_CHECK */

    audioCtxHandleT *ctx = (audioCtxHandleT*) afb_req_context_get(request);
    struct afb_arg arg = afb_req_get (request, "value");
    const char *value = arg.value;
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

static void play (struct afb_req request) {        /* AFB_SESSION_CHECK */

    audioCtxHandleT *ctx = (audioCtxHandleT*) afb_req_context_get(request);
    struct afb_arg arg = afb_req_get (request, "value");
    const char *value = arg.value;
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

static void ping (struct afb_req request) {         /* AFB_SESSION_NONE */
    afb_req_success (request, NULL, "Audio - Ping success");
}

static const struct AFB_restapi pluginApis[]= {
  {"init"    , AFB_SESSION_CHECK,  init      , "Audio API - init"},
  {"volume"  , AFB_SESSION_CHECK,  volume    , "Audio API - volume"},
  {"channels", AFB_SESSION_CHECK,  channels  , "Audio API - channels"},
  {"mute"    , AFB_SESSION_CHECK,  mute      , "Audio API - mute"},
  {"play"    , AFB_SESSION_CHECK,  play      , "Audio API - play"},
  {"ping"    , AFB_SESSION_NONE,   ping      , "Audio API - ping"},
  {NULL}
};

static const struct AFB_plugin pluginDesc = {
    .type   = AFB_PLUGIN_JSON,
    .info   = "Application Framework Binder - Audio plugin",
    .prefix = "audio",
    .apis   = pluginApis
};

const struct AFB_plugin *pluginRegister (const struct AFB_interface *itf)
{
	return &pluginDesc;
}
