/*
 * Copyright (C) 2015 "IoT.bzh"
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
#include "radio-rtlsdr.h"

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
STATIC json_object* freeRadio (AFB_clientCtx *client) {

    releaseRadio (client->plugin->handle, client->ctx);
    free (client->ctx);
    
    return jsonNewMessage (AFB_SUCCESS, "Released radio and client context");
}


/* ------ PUBLIC PLUGIN FUNCTIONS --------- */

STATIC json_object* power (AFB_request *request) {      /* AFB_SESSION_CREATE */
    
    pluginHandleT *handle = request->client->plugin->handle; 
    radioCtxHandleT *ctx = (radioCtxHandleT*)request->client->ctx;
    const char *value = getQueryValue (request, "value");
    json_object *jresp;

    /* create a private client context if needed */
    if (!ctx) ctx = initRadioCtx();

    /* no "?value=" parameter : return current state */
    if (!value) {
        jresp = json_object_new_object();
        ctx->radio ?
            json_object_object_add (jresp, "power", json_object_new_string ("on"))
          : json_object_object_add (jresp, "power", json_object_new_string ("off"));
    }

    /* "?value=" parameter is "1" or "on" */
    else if ( atoi(value) == 1 || !strcasecmp(value, "on") ) {
        if (!ctx->radio) {
            if (reserveRadio (handle, ctx) == AFB_FAIL) {
                request->errcode = MHD_HTTP_SERVICE_UNAVAILABLE;
                return (jsonNewMessage (AFB_FAIL, "No more radio devices available"));
            }
        }
        jresp = json_object_new_object();
        json_object_object_add (jresp, "power", json_object_new_string ("on"));
    }

    /* "?value=" parameter is "0" or "off" */
    else if ( atoi(value) == 0 || !strcasecmp(value, "off") ) {
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

    radioCtxHandleT *ctx = (radioCtxHandleT*)request->client->ctx;
    const char *value = getQueryValue (request, "value");
    json_object *jresp;

    /* no "?value=" parameter : return current state */
    if (!value) {
        jresp = json_object_new_object();
        ctx->mode ?
            json_object_object_add (jresp, "mode", json_object_new_string ("AM"))
          : json_object_object_add (jresp, "mode", json_object_new_string ("FM"));
    }

    /* "?value=" parameter is "1" or "on" */
    else if ( atoi(value) == 1 || !strcasecmp(value, "AM") ) {
        ctx->mode = AM;
        _radio_set_mode (ctx->idx, ctx->mode);

        jresp = json_object_new_object();
        json_object_object_add (jresp, "mode", json_object_new_string ("AM"));
    }

    /* "?value=" parameter is "0" or "off" */
    else if ( atoi(value) == 0 || !strcasecmp(value, "FM") ) {
        ctx->mode = FM;
        _radio_set_mode (ctx->idx, ctx->mode);

        jresp = json_object_new_object();
        json_object_object_add (jresp, "mode", json_object_new_string ("FM"));
    }
    
    return jresp;
}

STATIC json_object* freq (AFB_request *request) {        /* AFB_SESSION_CHECK */

    radioCtxHandleT *ctx = (radioCtxHandleT*)request->client->ctx;
    const char *value = getQueryValue (request, "value");
    json_object *jresp = json_object_new_object();
    char freq_str[256];

    /* no "?value=" parameter : return current state */
    if (!value) {
        snprintf (freq_str, sizeof(freq_str), "%f", ctx->freq);
        json_object_object_add (jresp, "freq", json_object_new_string (freq_str));
    }

    /* "?value=" parameter, set frequency */
    else {
        ctx->freq = strtof (value, NULL);
        _radio_set_freq (ctx->idx, ctx->freq);
        
        snprintf (freq_str, sizeof(freq_str), "%f", ctx->freq);
        json_object_object_add (jresp, "freq", json_object_new_string (freq_str));
    }
    
    return jresp;
}

STATIC json_object* mute (AFB_request *request) {        /* AFB_SESSION_CHECK */

    radioCtxHandleT *ctx = (radioCtxHandleT*)request->client->ctx;
    const char *value = getQueryValue (request, "value");
    json_object *jresp;
    char *mute_str;

    /* no "?value=" parameter : return current state */
    if (!value) {
        ctx->mute ?
            json_object_object_add (jresp, "mute", json_object_new_string ("on"))
          : json_object_object_add (jresp, "mute", json_object_new_string ("off"));
    }

    /* "?value=" parameter is "1" or "on" */
    else if ( atoi(value) == 1 || !strcasecmp(value, "on") ) {
        ctx->mute = 1;
        _radio_set_mute (ctx->idx, ctx->mute);
        
        jresp = json_object_new_object();
        json_object_object_add (jresp, "mute", json_object_new_string ("on"));
    }

    /* "?value=" parameter is "0" or "off" */
    else if ( atoi(value) == 0 || !strcasecmp(value, "off") ) {
        ctx->mute = 0;
        _radio_set_mute (ctx->idx, ctx->mute);
        
        jresp = json_object_new_object();
        json_object_object_add (jresp, "mute", json_object_new_string ("off"));
    }
    
    return jresp;
}

STATIC json_object* play (AFB_request *request) {        /* AFB_SESSION_CHECK */

    radioCtxHandleT *ctx = (radioCtxHandleT*)request->client->ctx;
    const char *value = getQueryValue (request, "value");
    json_object *jresp;
    
    /* no "?value=" parameter : return current state */
    if (!value) {
        ctx->is_playing ?
            json_object_object_add (jresp, "play", json_object_new_string ("on"))
          : json_object_object_add (jresp, "play", json_object_new_string ("off"));
    }

    /* "?value=" parameter is "1" or "on" */
    else if ( atoi(value) == 1 || !strcasecmp(value, "on") ) {
        /* radio playback */
        ctx->is_playing = 1;
        _radio_play (ctx->idx);

        jresp = json_object_new_object();
        json_object_object_add (jresp, "play", json_object_new_string ("on"));
    }

    /* "?value=" parameter is "0" or "off" */
    else if ( atoi(value) == 0 || !strcasecmp(value, "off") ) {
        /* radio stop */
        ctx->is_playing = 0;
        _radio_stop (ctx->idx);

        jresp = json_object_new_object();
        json_object_object_add (jresp, "play-on", json_object_new_string ("off"));
    }

    return jresp;
}

STATIC json_object* status (AFB_request *request) {
    return NULL;
}


STATIC AFB_restapi pluginApis[]= {
  {"power"  , AFB_SESSION_CREATE, (AFB_apiCB)power      , "Radio API - power"},
  {"mode"   , AFB_SESSION_CHECK,  (AFB_apiCB)mode       , "Radio API - mode"},
  {"freq"   , AFB_SESSION_CHECK,  (AFB_apiCB)freq       , "Radio API - freq"},
  {"mute"   , AFB_SESSION_CHECK,  (AFB_apiCB)mute       , "Radio API - mute"},
  {"play"   , AFB_SESSION_CHECK,  (AFB_apiCB)play       , "Radio API - play"},
  {"status" , AFB_SESSION_RENEW,  (AFB_apiCB)status     , "Radio API - status"},
  {NULL}
};

PUBLIC AFB_plugin* radioRegister () {
    AFB_plugin *plugin = malloc (sizeof(AFB_plugin));
    plugin->type  = AFB_PLUGIN_JSON;
    plugin->info  = "Application Framework Binder - Radio plugin";
    plugin->prefix  = "radio";
    plugin->apis  = pluginApis;

    plugin->handle = initRadioPlugin();
    plugin->freeCtxCB = freeRadio;

    return (plugin);
};
