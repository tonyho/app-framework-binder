/*
 * Copyright (C) 2016 "IoT.bzh"
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
#include <json-c/json.h>

#include "media-api.h"
#include "media-rygel.h"

#include "afb-plugin.h"
#include "afb-req-itf.h"

json_object* _rygel_list (mediaCtxHandleT *);

/* ------ LOCAL HELPER FUNCTIONS --------- */

/* private client context creation ; default values */
static mediaCtxHandleT* initMediaCtx () {

    mediaCtxHandleT *ctx;

    ctx = malloc (sizeof(mediaCtxHandleT));
    ctx->media_server = NULL;
    ctx->index = 0;

    return ctx;
}

/* ------ PUBLIC PLUGIN FUNCTIONS --------- */

static void init (struct afb_req request) {        /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = (mediaCtxHandleT*) afb_req_context_get(request);
    json_object *jresp;

    /* create a private client context */
    if (!ctx) {
        ctx = initMediaCtx();
        afb_req_context_set (request, ctx, free);
    }

    /* initialize server connection */
    if (!ctx->media_server)
      _rygel_init (ctx);

    jresp = json_object_new_object ();
    json_object_object_add (jresp, "init", json_object_new_string ("success"));
    afb_req_success (request, jresp, "Media - Initialized");
}

static void list (struct afb_req request) {        /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = (mediaCtxHandleT*) afb_req_context_get(request);
    json_object *jresp;

    jresp = _rygel_list (ctx);

    if (!jresp) {
      afb_req_fail (request, "failed", "no content found in media server");
      return;
    }

    afb_req_success (request, jresp, "Media - Listed");
}

static void selecting (struct afb_req request) {   /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = (mediaCtxHandleT*) afb_req_context_get(request);
    const char *value = afb_req_value (request, "value");
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
    else if (atoi(value) < 0) {
        afb_req_fail (request, "failed", "chosen index cannot be negatuve");
        return;
    }

    /* "?value=" parameter is positive */
    else if (atoi(value) >= 0) {
        index = (unsigned int) atoi(value);

        if (!_rygel_select (ctx, index)) {
          afb_req_fail (request, "failed", "chosen index superior to current media count");
          return;
        }

        ctx->index = index;
        jresp = json_object_new_object();
        json_object_object_add (jresp, "index", json_object_new_string (value));
    }
    else
        jresp = NULL;

    afb_req_success (request, jresp, "Media - Listed");
}

static void play (struct afb_req request) {        /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = (mediaCtxHandleT*) afb_req_context_get(request);
    json_object *jresp;

    if (!_rygel_do (ctx, PLAY, NULL)) {
      afb_req_fail (request, "failed", "could not play chosen media");
      return;
    }

    jresp = json_object_new_object ();
    json_object_object_add (jresp, "play", json_object_new_string ("success"));
    afb_req_success (request, jresp, "Media - Listed");
}

static void stop (struct afb_req request) {        /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = (mediaCtxHandleT*) afb_req_context_get(request);
    json_object *jresp;

    if (!_rygel_do (ctx, STOP, NULL)) {
      afb_req_fail (request, "failed", "could not stop chosen media");
      return;
    }

    jresp = json_object_new_object ();
    json_object_object_add (jresp, "stop", json_object_new_string ("success"));
    afb_req_success (request, jresp, "Media - Stopped");
}

static void pausing (struct afb_req request) {     /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = (mediaCtxHandleT*) afb_req_context_get(request);
    json_object *jresp;

    if (!_rygel_do (ctx, PAUSE, NULL)) {
      afb_req_fail (request, "failed", "could not pause chosen media");
      return;
    }

    jresp = json_object_new_object();
    json_object_object_add (jresp, "pause", json_object_new_string ("success"));
    afb_req_success (request, jresp, "Media - Paused");
}

static void seek (struct afb_req request) {        /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = (mediaCtxHandleT*) afb_req_context_get(request);
    const char *value = afb_req_value (request, "value");
    json_object *jresp;

    /* no "?value=" parameter : return error */
    if (!value) {
      afb_req_fail (request, "failed", "you must provide a time");
      return;
    }

    if (!_rygel_do (ctx, SEEK, (char *)value)) {
      afb_req_fail (request, "failed", "could not seek chosen media");
      return;
    }

    jresp = json_object_new_object();
    json_object_object_add (jresp, "seek", json_object_new_string ("success"));
    afb_req_success (request, jresp, "Media - Sought");
}

#if 0
static void upload (AFB_request *request, AFB_PostItem *item) { /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = (mediaCtxHandleT*) afb_req_context_get(request);
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
#endif

static void ping (struct afb_req request) {         /* AFB_SESSION_NONE */
    afb_req_success (request, NULL, "Media - Ping succeeded");
}


static const struct AFB_restapi pluginApis[]= {
  {"init"   , AFB_SESSION_CHECK,  init       , "Media API - init"   },
  {"list"   , AFB_SESSION_CHECK,  list       , "Media API - list"   },
  {"select" , AFB_SESSION_CHECK,  selecting  , "Media API - select" },
  {"play"   , AFB_SESSION_CHECK,  play       , "Media API - play"   },
  {"stop"   , AFB_SESSION_CHECK,  stop       , "Media API - stop"   },
  {"pause"  , AFB_SESSION_CHECK,  pausing    , "Media API - pause"  },
  {"seek"   , AFB_SESSION_CHECK,  seek       , "Media API - seek"   },
//  {"upload" , AFB_SESSION_CHECK,  (AFB_apiCB)upload     , "Media API - upload" },
  {"ping"   , AFB_SESSION_NONE,   ping       , "Media API - ping"   },
  {NULL}
};

static const struct AFB_plugin pluginDesc = {
    .type  = AFB_PLUGIN_JSON,
    .info  = "Application Framework Binder - Media plugin",
    .prefix  = "media",
    .apis  = pluginApis
};

const struct AFB_plugin *pluginRegister (const struct AFB_interface *itf)
{
	return &pluginDesc;
}
