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
STATIC void freeMedia (void *context) {

    free (context);
}

/* ------ PUBLIC PLUGIN FUNCTIONS --------- */

STATIC json_object* init (AFB_request *request) {        /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx;
    json_object *jresp;

    /* create a private client context */
    if (!request->context)
        request->context = initMediaCtx();

    ctx = (mediaCtxHandleT*)request->context;

    /* initialize server connection */
    if (!ctx->media_server)
      _rygel_init (request->context);

    jresp = json_object_new_object();
    json_object_object_add(jresp, "info", json_object_new_string ("Media initialized"));
    return jresp;
}

STATIC json_object* list (AFB_request *request) {        /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = (mediaCtxHandleT*)request->context;
    json_object *jresp;

    jresp = _rygel_list (ctx);

    if (!jresp)
      return jsonNewMessage(AFB_FAIL, "No content found in media server");

    return jresp;
}

STATIC json_object* selecting (AFB_request *request) {   /* AFB_SESSION_CHECK */

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

        if (!_rygel_select (ctx, index))
          return jsonNewMessage(AFB_FAIL, "Chosen index superior to current media count");

        ctx->index = index;
        jresp = json_object_new_object();
        json_object_object_add (jresp, "index", json_object_new_string (value));
    }
    else
        jresp = NULL;

    return jresp;
}

STATIC json_object* play (AFB_request *request) {        /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = (mediaCtxHandleT*)request->context;

    if (!_rygel_do (ctx, PLAY, NULL))
      return jsonNewMessage(AFB_FAIL, "Could not play chosen media");

    return jsonNewMessage(AFB_SUCCESS, "PLaying media");
}

STATIC json_object* stop (AFB_request *request) {        /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = (mediaCtxHandleT*)request->context;

    if (!_rygel_do (ctx, STOP, NULL))
      return jsonNewMessage(AFB_FAIL, "Could not stop chosen media");

    return jsonNewMessage(AFB_SUCCESS, "Stopped media");
}

STATIC json_object* pausing (AFB_request *request) {     /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = (mediaCtxHandleT*)request->context;

    if (!_rygel_do (ctx, PAUSE, NULL))
      return jsonNewMessage(AFB_FAIL, "Could not pause chosen media");

    return jsonNewMessage(AFB_SUCCESS, "Paused media");
}

STATIC json_object* seek (AFB_request *request) {        /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = (mediaCtxHandleT*)request->context;
    const char *value = getQueryValue (request, "value");

    /* no "?value=" parameter : return error */
    if (!value)
      return jsonNewMessage(AFB_FAIL, "You must provide a time");

    if (!_rygel_do (ctx, SEEK, value))
      return jsonNewMessage(AFB_FAIL, "Could not seek chosen media");

    return jsonNewMessage(AFB_SUCCESS, "Seeked media");
}

STATIC json_object* upload (AFB_request *request, AFB_PostItem *item) { /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = (mediaCtxHandleT*)request->context;
    AFB_PostCtx *postFileCtx;
    json_object *jresp;
    char *path;

    /* item is !NULL until transfer is complete */
    if (item != NULL)
      return getPostFile (request, item, "media");

    /* target intermediary file path */
    path = getPostPath (request);

    if (!path)
        fprintf (stderr, "Error encoutered during intermediary file transfer\n");

    else if (!_rygel_upload (ctx, path)) {
        request->errcode = MHD_HTTP_EXPECTATION_FAILED;
        request->jresp = jsonNewMessage (AFB_FAIL, "Error when uploading file to media server... could not complete");
    }

    else {
        request->errcode = MHD_HTTP_OK;
        request->jresp = jsonNewMessage (AFB_SUCCESS, "upload=%s done", path);
    }

    /* finalizes file transfer */
    return getPostFile (request, item, NULL);
}

STATIC json_object* ping (AFB_request *request) {         /* AFB_SESSION_NONE */
    return jsonNewMessage(AFB_SUCCESS, "Ping Binder Daemon - Media API");
}


STATIC AFB_restapi pluginApis[]= {
  {"init"   , AFB_SESSION_CHECK,  (AFB_apiCB)init       , "Media API - init"   },
  {"list"   , AFB_SESSION_CHECK,  (AFB_apiCB)list       , "Media API - list"   },
  {"select" , AFB_SESSION_CHECK,  (AFB_apiCB)selecting  , "Media API - select" },
  {"play"   , AFB_SESSION_CHECK,  (AFB_apiCB)play       , "Media API - play"   },
  {"stop"   , AFB_SESSION_CHECK,  (AFB_apiCB)stop       , "Media API - stop"   },
  {"pause"  , AFB_SESSION_CHECK,  (AFB_apiCB)pausing    , "Media API - pause"  },
  {"seek"   , AFB_SESSION_CHECK,  (AFB_apiCB)seek       , "Media API - seek"   },
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
