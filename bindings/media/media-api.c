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

#include <string.h>

#include <json-c/json.h>

#include "media-api.h"
#include "media-rygel.h"

#include <afb/afb-binding.h>
#include <afb/afb-req-itf.h>

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

    mediaCtxHandleT *ctx = afb_req_context_get(request);
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

    mediaCtxHandleT *ctx = afb_req_context_get(request);
    json_object *jresp;

    /* check that context is initialized */
    if (ctx == NULL) {
      afb_req_fail (request, "failed", "uninitialized");
      return;
    }

    jresp = _rygel_list (ctx);

    if (!jresp) {
      afb_req_fail (request, "failed", "no content found in media server");
      return;
    }

    afb_req_success (request, jresp, "Media - Listed");
}

static void selecting (struct afb_req request) {   /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = afb_req_context_get(request);
    const char *value = afb_req_value (request, "value");
    json_object *jresp;
    unsigned int index;
    char index_str[5];

    /* check that context is initialized */
    if (ctx == NULL) {
      afb_req_fail (request, "failed", "uninitialized");
      return;
    }

    /* no "?value=" parameter : return current index */
    if (!value) {
        snprintf (index_str, sizeof(index_str), "%d", ctx->index);
        jresp = json_object_new_object();
        json_object_object_add (jresp, "index", json_object_new_string (index_str));
    }

    /* "?value=" parameter is negative */
    else if (atoi(value) < 0) {
        afb_req_fail (request, "failed", "chosen index cannot be negative");
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

    mediaCtxHandleT *ctx = afb_req_context_get(request);
    json_object *jresp;

    /* check that context is initialized */
    if (ctx == NULL) {
      afb_req_fail (request, "failed", "uninitialized");
      return;
    }

    if (!_rygel_do (ctx, PLAY, NULL)) {
      afb_req_fail (request, "failed", "could not play chosen media");
      return;
    }

    jresp = json_object_new_object ();
    json_object_object_add (jresp, "play", json_object_new_string ("success"));
    afb_req_success (request, jresp, "Media - Listed");
}

static void stop (struct afb_req request) {        /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = afb_req_context_get(request);
    json_object *jresp;

    /* check that context is initialized */
    if (ctx == NULL) {
      afb_req_fail (request, "failed", "uninitialized");
      return;
    }

    if (!_rygel_do (ctx, STOP, NULL)) {
      afb_req_fail (request, "failed", "could not stop chosen media");
      return;
    }

    jresp = json_object_new_object ();
    json_object_object_add (jresp, "stop", json_object_new_string ("success"));
    afb_req_success (request, jresp, "Media - Stopped");
}

static void pausing (struct afb_req request) {     /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = afb_req_context_get(request);
    json_object *jresp;

    /* check that context is initialized */
    if (ctx == NULL) {
      afb_req_fail (request, "failed", "uninitialized");
      return;
    }

    if (!_rygel_do (ctx, PAUSE, NULL)) {
      afb_req_fail (request, "failed", "could not pause chosen media");
      return;
    }

    jresp = json_object_new_object();
    json_object_object_add (jresp, "pause", json_object_new_string ("success"));
    afb_req_success (request, jresp, "Media - Paused");
}

static void seek (struct afb_req request) {        /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = afb_req_context_get(request);
    const char *value = afb_req_value (request, "value");
    json_object *jresp;

    /* check that context is initialized */
    if (ctx == NULL) {
      afb_req_fail (request, "failed", "uninitialized");
      return;
    }

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

static char *renamed_filename(struct afb_arg argfile)
{
    char *result;
    const char *e = strrchr(argfile.path, '/');
    if (e == NULL)
        result = strdup(argfile.value);
    else {
        result = malloc((++e - argfile.path) + strlen(argfile.value) + 1);
        if (result != NULL)
            strcpy(stpncpy(result, argfile.path, e - argfile.path), argfile.value);
    }
    return result;
}

static void on_uploaded(struct afb_req *prequest, int status)
{
    struct afb_req request = afb_req_unstore(prequest);
    struct afb_arg argfile = afb_req_get(request, "file-upload");
    char *file = renamed_filename(argfile);
    if (file != NULL)
        unlink(file);
    free(file);
    if (status)
        afb_req_fail (request, "failed", "expected file not received");
    else
        afb_req_success_f (request, NULL, "uploaded file %s", argfile.value);
   afb_req_unref(request);
}

static void upload (struct afb_req request) { /* AFB_SESSION_CHECK */

    mediaCtxHandleT *ctx = afb_req_context_get(request);
    struct afb_req *prequest;
    struct afb_arg argfile;
    char *path;

    /* check that context is initialized */
    if (ctx == NULL) {
      afb_req_fail (request, "failed", "uninitialized");
      return;
    }

    /* get the file */
    argfile = afb_req_get(request, "file-upload");
    if (!argfile.value || !argfile.path) {
        afb_req_fail (request, "failed", "expected file not received");
        return;
    }

    /* rename the file */
    path = renamed_filename(argfile);
    if (path == NULL) {
        afb_req_fail (request, "failed", "out of memory");
        return;
    }
    if (rename(argfile.path, path) != 0) {
        free(path);
        afb_req_fail (request, "failed", "system error");
        return;
    }

    /* for asynchronous processing */
    prequest = afb_req_store(request);
    if (path == NULL) {
        unlink(path);
        afb_req_fail (request, "failed", "out of memory");
    }
    else if (!_rygel_upload (ctx, path, (void*)on_uploaded, prequest)) {
        afb_req_unref(afb_req_unstore(prequest));
        unlink(path);
        afb_req_fail (request, "failed", "Error when uploading file to media server... could not complete");
    }
    free(path);
}

static void ping (struct afb_req request) {         /* AFB_SESSION_NONE */
    afb_req_success (request, NULL, "Media - Ping succeeded");
}


static const struct afb_verb_desc_v1 verbs[]= {
  {"init"   , AFB_SESSION_CHECK,  init       , "Media API - init"   },
  {"list"   , AFB_SESSION_CHECK,  list       , "Media API - list"   },
  {"select" , AFB_SESSION_CHECK,  selecting  , "Media API - select" },
  {"play"   , AFB_SESSION_CHECK,  play       , "Media API - play"   },
  {"stop"   , AFB_SESSION_CHECK,  stop       , "Media API - stop"   },
  {"pause"  , AFB_SESSION_CHECK,  pausing    , "Media API - pause"  },
  {"seek"   , AFB_SESSION_CHECK,  seek       , "Media API - seek"   },
//  {"upload" , AFB_SESSION_CHECK,  upload     , "Media API - upload" },
  {"ping"   , AFB_SESSION_NONE,   ping       , "Media API - ping"   },
  {NULL}
};

static const struct afb_binding pluginDesc = {
    .type   = AFB_BINDING_VERSION_1,
    .v1 = {
        .info   = "Application Framework Binder - Media plugin",
        .prefix = "media",
        .verbs   = verbs
    }
};

const struct afb_binding *afbBindingV1Register (const struct afb_binding_interface *itf)
{
    return &pluginDesc;
}
