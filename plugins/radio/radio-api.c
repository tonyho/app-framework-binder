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
#include <strings.h>
#include <json-c/json.h>

#include "radio-api.h"
#include "radio-rtlsdr.h"

#include "afb-plugin.h"
#include "afb-req-itf.h"

/* ********************************************************

   FULUP integration proposal with client session context

   ******************************************************** */

/* ------ LOCAL HELPER FUNCTIONS --------- */

static pluginHandleT *the_radio = NULL;

/* detect new radio devices */
void updateRadioDevList(pluginHandleT *handle) {

  int idx;  

  // loop on existing radio if any
  for (idx = 0; idx < _radio_dev_count(); idx++) {
      if (idx == MAX_RADIO) break;
      handle->radios[idx] = calloc(1, sizeof(radioDevT)); /* use calloc to set used to FALSE */
      handle->radios[idx]->name = (char *) _radio_dev_name(idx); 
  }
  handle->devCount = _radio_dev_count();
}

/* global plugin context creation ; at loading time [radio devices might not be visible] */
static void initRadioPlugin() {

  pluginHandleT *handle = the_radio;

  handle = calloc (1, sizeof(pluginHandleT));
  updateRadioDevList (handle);
}

/* private client context creation ; default values */
static radioCtxHandleT* initRadioCtx () {

    radioCtxHandleT *ctx;

    ctx = malloc (sizeof(radioCtxHandleT));
    ctx->radio = NULL;
    ctx->idx = -1;
    ctx->mode = FM;
    ctx->freq = 100.0;
    ctx->mute = 0;
    ctx->is_playing = 0;

    return ctx;
}

/* reserve a radio device for requesting client, power it on */
unsigned char reserveRadio (pluginHandleT *handle, radioCtxHandleT *ctx) {
    unsigned int idx;

    /* loop on all devices, find an unused one */
    for (idx = 0; idx < _radio_dev_count(); idx++) {
        if (idx == MAX_RADIO) break;
        if (handle->radios[idx]->used == FALSE) goto found_radio; /* found one */
    }
    return 0;

   found_radio:
    /* try to power it on, passing client context info such as frequency... */
    _radio_on (idx, ctx);
    /* TODO : try to re-iterate from the next ones if it failed ! */

    /* globally mark it as reserved */
    handle->radios[idx]->used = TRUE;

    /* store relevant info to client context (direct pointer, index) */
    ctx->radio = handle->radios[idx];
    ctx->idx = idx;

    return 1;
}

/* free a radio device from requesting client, power it off */
unsigned char releaseRadio (pluginHandleT *handle, radioCtxHandleT *ctx) {

    /* stop playing if it was doing this (blocks otherwise) */
    if (ctx->is_playing) {
        ctx->is_playing = 0;
        _radio_stop (ctx->idx);
    }

    /* power it off */
    _radio_off (ctx->idx);

    /* globally mark it as free */
    handle->radios[ctx->idx]->used = FALSE;

    /* clean client context */
    ctx->radio = NULL;
    ctx->idx = -1;

    return 1;
}

/* called when client session dies [e.g. client quits for more than 15mns] */
static void freeRadio (void *context) {

    releaseRadio (the_radio, context);
    free (context);
}


/* ------ PUBLIC PLUGIN FUNCTIONS --------- */

static void init (struct afb_req request) {        /* AFB_SESSION_CHECK */

    radioCtxHandleT *ctx = (radioCtxHandleT*) afb_req_context_get(request);
    json_object *jresp;

    /* create a private client context */
    if (!ctx) {
        ctx = initRadioCtx();
        afb_req_context_set (request, ctx, free);
    }

    jresp = json_object_new_object();
    json_object_object_add(jresp, "init", json_object_new_string ("success"));
    afb_req_success (request, jresp, "Radio - Initialized");
}

static void power (struct afb_req request) {       /* AFB_SESSION_CHECK */

    pluginHandleT *handle = the_radio;
    radioCtxHandleT *ctx = (radioCtxHandleT*) afb_req_context_get(request);
    const char *value = afb_req_value (request, "value");
    json_object *jresp;

    /* no "?value=" parameter : return current state */
    if (!value) {
        jresp = json_object_new_object();
        ctx->radio ?
            json_object_object_add (jresp, "power", json_object_new_string ("on"))
          : json_object_object_add (jresp, "power", json_object_new_string ("off"));
    }

    /* "?value=" parameter is "1" or "true" */
    else if ( atoi(value) == 1 || !strcasecmp(value, "true") ) {
        if (!ctx->radio) {
            if (!reserveRadio (handle, ctx)) {
                afb_req_fail (request, "failed", "no more radio devices available");
		        return;
            }
        }
        jresp = json_object_new_object();
        json_object_object_add (jresp, "power", json_object_new_string ("on"));
    }

    /* "?value=" parameter is "0" or "false" */
    else if ( atoi(value) == 0 || !strcasecmp(value, "false") ) {
        if (ctx->radio) {
            if (!releaseRadio (handle, ctx)) {
                afb_req_fail (request, "failed", "Unable to release radio device");
		        return;
            }
        }
        jresp = json_object_new_object();
        json_object_object_add (jresp, "power", json_object_new_string ("off"));
    }
    else
        jresp = NULL;

    afb_req_success (request, jresp, "Radio - Power set");
}

static void mode (struct afb_req request) {        /* AFB_SESSION_CHECK */

    radioCtxHandleT *ctx = (radioCtxHandleT*) afb_req_context_get(request);
    const char *value = afb_req_value (request, "value");
    json_object *jresp = json_object_new_object();

    /* no "?value=" parameter : return current state */
    if (!value || !ctx->radio) {
        ctx->mode ?
            json_object_object_add (jresp, "mode", json_object_new_string ("AM"))
          : json_object_object_add (jresp, "mode", json_object_new_string ("FM"));
    }

    /* "?value=" parameter is "1" or "AM" */
    else if ( atoi(value) == 1 || !strcasecmp(value, "AM") ) {
        ctx->mode = AM;
        _radio_set_mode (ctx->idx, ctx->mode);
        json_object_object_add (jresp, "mode", json_object_new_string ("AM"));
    }

    /* "?value=" parameter is "0" or "FM" */
    else if ( atoi(value) == 0 || !strcasecmp(value, "FM") ) {
        ctx->mode = FM;
        _radio_set_mode (ctx->idx, ctx->mode);
        json_object_object_add (jresp, "mode", json_object_new_string ("FM"));
    }

    afb_req_success (request, jresp, "Radio - Mode set");
}

static void freq (struct afb_req request) {        /* AFB_SESSION_CHECK */

    radioCtxHandleT *ctx = (radioCtxHandleT*) afb_req_context_get(request);
    const char *value = afb_req_value (request, "value");
    json_object *jresp = json_object_new_object();
    double freq;
    char freq_str[256];

    /* no "?value=" parameter : return current state */
    if (!value || !ctx->radio) {
        snprintf (freq_str, sizeof(freq_str), "%f", ctx->freq);
        json_object_object_add (jresp, "freq", json_object_new_string (freq_str));
    }

    /* "?value=" parameter, set frequency */
    else {
        freq = strtod (value, NULL);
        _radio_set_freq (ctx->idx, freq);
        ctx->freq = (float)freq;

        snprintf (freq_str, sizeof(freq_str), "%f", ctx->freq);
        json_object_object_add (jresp, "freq", json_object_new_string (freq_str));
    }

    afb_req_success (request, jresp, "Radio - Frequency Set");
}

static void mute (struct afb_req request) {        /* AFB_SESSION_CHECK */

    radioCtxHandleT *ctx = (radioCtxHandleT*) afb_req_context_get(request);
    const char *value = afb_req_value (request, "value");
    json_object *jresp = json_object_new_object();

    /* no "?value=" parameter : return current state */
    if (!value || !ctx->radio) {
        ctx->mute ?
            json_object_object_add (jresp, "mute", json_object_new_string ("on"))
          : json_object_object_add (jresp, "mute", json_object_new_string ("off"));
    }

    /* "?value=" parameter is "1" or "true" */
    else if ( atoi(value) == 1 || !strcasecmp(value, "true") ) {
        ctx->mute = 1;
        _radio_set_mute (ctx->idx, ctx->mute);
        json_object_object_add (jresp, "mute", json_object_new_string ("on"));
    }

    /* "?value=" parameter is "0" or "false" */
    else if ( atoi(value) == 0 || !strcasecmp(value, "off") ) {
        ctx->mute = 0;
        _radio_set_mute (ctx->idx, ctx->mute);
        json_object_object_add (jresp, "mute", json_object_new_string ("off"));
    }

    afb_req_success (request, jresp, "Radio - Mute set"); 
}

static void play (struct afb_req request) {        /* AFB_SESSION_CHECK */

    radioCtxHandleT *ctx = (radioCtxHandleT*) afb_req_context_get(request);
    const char *value = afb_req_value (request, "value");
    json_object *jresp = json_object_new_object();
    
    /* no "?value=" parameter : return current state */
    if (!value || !ctx->radio) {
        ctx->is_playing ?
            json_object_object_add (jresp, "play", json_object_new_string ("on"))
          : json_object_object_add (jresp, "play", json_object_new_string ("off"));
    }

    /* "?value=" parameter is "1" or "true" */
    else if ( atoi(value) == 1 || !strcasecmp(value, "true") ) {
        /* radio playback */
        ctx->is_playing = 1;
        _radio_play (ctx->idx);
        json_object_object_add (jresp, "play", json_object_new_string ("on"));
    }

    /* "?value=" parameter is "0" or "false" */
    else if ( atoi(value) == 0 || !strcasecmp(value, "false") ) {
        /* radio stop */
        ctx->is_playing = 0;
        _radio_stop (ctx->idx);
        json_object_object_add (jresp, "play", json_object_new_string ("off"));
    }

    afb_req_success (request, jresp, "Radio - Play succeeded");
}

static void ping (struct afb_req request) {         /* AFB_SESSION_NONE */
    afb_req_success (request, NULL, "Radio - Ping succeeded");
}


static const struct AFB_restapi pluginApis[]= {
  {"init"   , AFB_SESSION_CHECK,  init       , "Radio API - init"},
  {"power"  , AFB_SESSION_CHECK,  power      , "Radio API - power"},
  {"mode"   , AFB_SESSION_CHECK,  mode       , "Radio API - mode"},
  {"freq"   , AFB_SESSION_CHECK,  freq       , "Radio API - freq"},
  {"mute"   , AFB_SESSION_CHECK,  mute       , "Radio API - mute"},
  {"play"   , AFB_SESSION_CHECK,  play       , "Radio API - play"},
  {"ping"   , AFB_SESSION_NONE,   ping       , "Radio API - ping"},
  {NULL}
};

static const struct AFB_plugin pluginDesc = {
    .type  = AFB_PLUGIN_JSON,
    .info  = "Application Framework Binder - Radio plugin",
    .prefix  = "radio",
    .apis  = pluginApis
};

const struct AFB_plugin *pluginRegister (const struct AFB_interface *itf)
{
    initRadioPlugin();
	return &pluginDesc;
}
