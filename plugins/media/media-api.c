/*
 * Copyright (C) 2016 "IoT.bzh"
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

#include "media-api.h"

/* ------ LOCAL HELPER FUNCTIONS --------- */

/* private client context creation ; default values */
STATIC mediaCtxHandleT* initMediaCtx () {

    mediaCtxHandleT *ctx;

    ctx = malloc (sizeof(mediaCtxHandleT));
    ctx->media_server = NULL;
    ctx->index = 0;

    return ctx;
}

/* called when client session dies [e.g. client quits for more than 15mns] */
STATIC void freeMedia (void *context, void *handle) {

    free (context);
}

/* ------ PUBLIC PLUGIN FUNCTIONS --------- */

STATIC json_object* init (AFB_request *request) {        /* AFB_SESSION_CHECK */

    json_object *jresp;

    /* create a private client context */
    if (!request->context)
        request->context = initMediaCtx();

    /* initialize server connection */
    _rygel_init (request->context);

    jresp = json_object_new_object();
    json_object_object_add(jresp, "info", json_object_new_string ("Media initialized"));
    return jresp;
}

STATIC json_object* list (AFB_request *request) {        /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = (mediaCtxHandleT*)request->context;
    json_object *jresp;
    char *result;

    result = _rygel_list (ctx);

    if (!result)
      return jsonNewMessage(AFB_FAIL, "No content found in media server");

    jresp = json_object_new_object();
    json_object_object_add(jresp, "list", json_object_new_string (result));
    return jresp;
}

STATIC json_object* choose (AFB_request *request) {      /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = (mediaCtxHandleT*)request->context;
    const char *value = getQueryValue (request, "value");
    json_object *jresp;
    unsigned int index;
    char index_str[5];

    /* no "?value=" parameter : return current index */
    if (!value) {
        snprintf (index_str, sizeof(index_str), "%d", ctx->index);
        jresp = json_object_new_object();
        json_object_object_add (jresp, "index", json_object_new_string (index_str));
    }

    /* "?value=" parameter is negative */
    else if (atoi(value) < 0)
        return jsonNewMessage(AFB_FAIL, "Chosen index cannot be negative");

    /* "?value=" parameter is positive */
    else if (atoi(value) >= 0) {
        index = (unsigned int) atoi(value);

        if (!_rygel_choose (ctx, index))
          return jsonNewMessage(AFB_FAIL, "Chosen index superior to current media count");

        ctx->index = index;
        jresp = json_object_new_object();
        json_object_object_add (jresp, "index", json_object_new_string (value));
    }

    return jresp;
}

STATIC json_object* play (AFB_request *request) {        /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = (mediaCtxHandleT*)request->context;

    if (!_rygel_do (ctx, PLAY))
      return jsonNewMessage(AFB_FAIL, "Could not play chosen media");

    return jsonNewMessage(AFB_SUCCESS, "PLaying media");
}

STATIC json_object* stop (AFB_request *request) {        /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = (mediaCtxHandleT*)request->context;

    if (!_rygel_do (ctx, STOP))
      return jsonNewMessage(AFB_FAIL, "Could not stop chosen media");

    return jsonNewMessage(AFB_SUCCESS, "Stopped media");
}

STATIC json_object* paused (AFB_request *request) {      /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = (mediaCtxHandleT*)request->context;

    if (!_rygel_do (ctx, PAUSE))
      return jsonNewMessage(AFB_FAIL, "Could not pause chosen media");

    return jsonNewMessage(AFB_SUCCESS, "Paused media");
}

STATIC json_object* upload (AFB_request *request) {      /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = (mediaCtxHandleT*)request->context;
    const char *value = getQueryValue (request, "value");
    json_object *jresp;
    char path[256];

    /* no "?value=" parameter : return error */
    if (!value)
      return jsonNewMessage(AFB_FAIL, "You must provide a file name");

    snprintf (path, sizeof(path), "/tmp/%s", value);
    if (access (path, R_OK) == -1)
      return jsonNewMessage(AFB_FAIL, "File not found");

    if (!_rygel_upload (ctx, path))
      return jsonNewMessage(AFB_FAIL, "Error when uploading file... could not complete");

    return jsonNewMessage(AFB_SUCCESS, "File successfully uploaded");
}

STATIC json_object* ping (AFB_request *request) {         /* AFB_SESSION_NONE */
    return jsonNewMessage(AFB_SUCCESS, "Ping Binder Daemon - Media API");
}


STATIC AFB_restapi pluginApis[]= {
  {"init"   , AFB_SESSION_CHECK,  (AFB_apiCB)init       , "Media API - init"   },
  {"list"   , AFB_SESSION_CHECK,  (AFB_apiCB)list       , "Media API - list"   },
  {"choose" , AFB_SESSION_CHECK,  (AFB_apiCB)choose     , "Media API - choose" },
  {"play"   , AFB_SESSION_CHECK,  (AFB_apiCB)play       , "Media API - play"   },
  {"stop"   , AFB_SESSION_CHECK,  (AFB_apiCB)stop       , "Media API - stop"   },
  {"pause"  , AFB_SESSION_CHECK,  (AFB_apiCB)paused     , "Media API - pause"  },
  {"upload" , AFB_SESSION_CHECK,  (AFB_apiCB)upload     , "Media API - upload" },
  {"ping"   , AFB_SESSION_NONE,   (AFB_apiCB)ping       , "Media API - ping"   },
  {NULL}
};

PUBLIC AFB_plugin* pluginRegister () {
    AFB_plugin *plugin = malloc (sizeof(AFB_plugin));
    plugin->type  = AFB_PLUGIN_JSON;
    plugin->info  = "Application Framework Binder - Media plugin";
    plugin->prefix  = "media";
    plugin->apis  = pluginApis;

    /*plugin->handle = initRadioPlugin();*/
    plugin->freeCtxCB = (AFB_freeCtxCB)freeMedia;

    return (plugin);
};
