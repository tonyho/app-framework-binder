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

#include "radio-api.h"

/* ********************************************************

   FULUP integration proposal with client session context

   ******************************************************** */

/* ------ LOCAL HELPER FUNCTIONS --------- */

/* detect new radio devices */
STATIC void updateRadioDevList(pluginHandleT *handle) {

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
STATIC pluginHandleT* initRadioPlugin() {

  pluginHandleT *handle;

  handle = calloc (1, sizeof(pluginHandleT));
  updateRadioDevList (handle);

  return handle;
}

/* private client context creation ; default values */
STATIC radioCtxHandleT* initRadioCtx () {

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
STATIC AFB_error reserveRadio (pluginHandleT *handle, radioCtxHandleT *ctx) {
    int idx;

    /* loop on all devices, find an unused one */
    for (idx = 0; idx < _radio_dev_count(); idx++) {
        if (idx == MAX_RADIO) break;
        if (handle->radios[idx]->used == FALSE) goto found_radio; /* found one */
    }
    return AFB_FAIL;

   found_radio:
    /* try to power it on, passing client context info such as frequency... */
    _radio_on (idx, ctx);
    /* TODO : try to re-iterate from the next ones if it failed ! */

    /* globally mark it as reserved */
    handle->radios[idx]->used = TRUE;

    /* store relevant info to client context (direct pointer, index) */
    ctx->radio = handle->radios[idx];
    ctx->idx = idx;

    return AFB_SUCCESS;
}

/* free a radio device from requesting client, power it off */
STATIC AFB_error releaseRadio (pluginHandleT *handle, radioCtxHandleT *ctx) {

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

    return AFB_SUCCESS;
}

/* called when client session dies [e.g. client quits for more than 15mns] */
STATIC void freeRadio (void *context, void *handle) {

    releaseRadio (handle, context);
    free (context);
}


/* ------ PUBLIC PLUGIN FUNCTIONS --------- */

STATIC json_object* init (AFB_request *request) {        /* AFB_SESSION_CHECK */

    json_object *jresp;

    /* create a private client context */
    if (!request->context)
        request->context = initRadioCtx();

    jresp = json_object_new_object();
    json_object_object_add(jresp, "info", json_object_new_string ("Radio initialized"));
    return jresp;
}

STATIC json_object* power (AFB_request *request) {       /* AFB_SESSION_CHECK */

    pluginHandleT *handle = (pluginHandleT*)request->handle;
    radioCtxHandleT *ctx = (radioCtxHandleT*)request->context;
    const char *value = getQueryValue (request, "value");
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
            if (reserveRadio (handle, ctx) == AFB_FAIL) {
                request->errcode = MHD_HTTP_SERVICE_UNAVAILABLE;
                return (jsonNewMessage (AFB_FAIL, "No more radio devices available"));
            }
        }
        jresp = json_object_new_object();
        json_object_object_add (jresp, "power", json_object_new_string ("on"));
    }

    /* "?value=" parameter is "0" or "false" */
    else if ( atoi(value) == 0 || !strcasecmp(value, "false") ) {
        if (ctx->radio) {
            if (releaseRadio (handle, ctx) == AFB_FAIL) {
                request->errcode = MHD_HTTP_SERVICE_UNAVAILABLE;
                return (jsonNewMessage (AFB_FAIL, "Unable to release radio device"));
            }
        }
        jresp = json_object_new_object();
        json_object_object_add (jresp, "power", json_object_new_string ("off"));
    }

    return jresp;
}

STATIC json_object* mode (AFB_request *request) {        /* AFB_SESSION_CHECK */

    radioCtxHandleT *ctx = (radioCtxHandleT*)request->context;
    const char *value = getQueryValue (request, "value");
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
    
    return jresp;
}

STATIC json_object* freq (AFB_request *request) {        /* AFB_SESSION_CHECK */

    radioCtxHandleT *ctx = (radioCtxHandleT*)request->context;
    const char *value = getQueryValue (request, "value");
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
    
    return jresp;
}

STATIC json_object* mute (AFB_request *request) {        /* AFB_SESSION_CHECK */

    radioCtxHandleT *ctx = (radioCtxHandleT*)request->context;
    const char *value = getQueryValue (request, "value");
    json_object *jresp = json_object_new_object();
    char *mute_str;

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
    
    return jresp;
}

STATIC json_object* play (AFB_request *request) {        /* AFB_SESSION_CHECK */

    radioCtxHandleT *ctx = (radioCtxHandleT*)request->context;
    const char *value = getQueryValue (request, "value");
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

    return jresp;
}

STATIC json_object* ping (AFB_request *request) {         /* AFB_SESSION_NONE */
    return jsonNewMessage(AFB_SUCCESS, "Ping Binder Daemon - Radio API");
}


STATIC AFB_restapi pluginApis[]= {
  {"init"   , AFB_SESSION_CHECK,  (AFB_apiCB)init       , "Radio API - init"},
  {"power"  , AFB_SESSION_CHECK,  (AFB_apiCB)power      , "Radio API - power"},
  {"mode"   , AFB_SESSION_CHECK,  (AFB_apiCB)mode       , "Radio API - mode"},
  {"freq"   , AFB_SESSION_CHECK,  (AFB_apiCB)freq       , "Radio API - freq"},
  {"mute"   , AFB_SESSION_CHECK,  (AFB_apiCB)mute       , "Radio API - mute"},
  {"play"   , AFB_SESSION_CHECK,  (AFB_apiCB)play       , "Radio API - play"},
  {"ping"   , AFB_SESSION_NONE,   (AFB_apiCB)ping       , "Radio API - ping"},
  {NULL}
};

PUBLIC AFB_plugin* pluginRegister () {
    AFB_plugin *plugin = malloc (sizeof(AFB_plugin));
    plugin->type  = AFB_PLUGIN_JSON;
    plugin->info  = "Application Framework Binder - Radio plugin";
    plugin->prefix  = "radio";
    plugin->apis  = pluginApis;

    plugin->handle = initRadioPlugin();
    plugin->freeCtxCB = (AFB_freeCtxCB)freeRadio;

    return (plugin);
};
