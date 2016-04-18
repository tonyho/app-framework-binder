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

#ifndef MEDIA_RYGEL_H
#define MEDIA_RYGEL_H

/* --------------- MEDIA RYGEL DEFINITIONS ------------------ */

#include <sys/time.h>
#include <libgupnp/gupnp-control-point.h>
#include <libgupnp-av/gupnp-av.h>

#include "local-def.h"

#define URN_MEDIA_SERVER   "urn:schemas-upnp-org:device:MediaServer:1"
#define URN_MEDIA_RENDERER "urn:schemas-upnp-org:device:MediaRenderer:1"
#define URN_CONTENT_DIR    "urn:schemas-upnp-org:service:ContentDirectory"
#define URN_AV_TRANSPORT   "urn:schemas-upnp-org:service:AVTransport"

typedef enum { PLAY, PAUSE, STOP, SEEK } State;
typedef struct dev_ctx dev_ctx_T;

struct dev_ctx {
    GMainContext *loop;
    GUPnPContext *context;
    GUPnPDeviceInfo *device_info;
    GUPnPServiceInfo *content_dir;
    GUPnPServiceInfo *av_transport;
    char *content_res;
    int content_num;
    State state;
    State target_state;
    char *action_args;
    char *transfer_path;
    unsigned char transfer_started;
};

STATIC char* _rygel_list_raw (dev_ctx_T *, unsigned int *);
STATIC char* _rygel_find_upload_id (dev_ctx_T *, char *);
STATIC char* _rygel_find_id_for_index (dev_ctx_T *, char *, unsigned int);
STATIC char* _rygel_find_metadata_for_id (dev_ctx_T *, char *);
STATIC char* _rygel_find_uri_for_metadata (dev_ctx_T *, char *);
STATIC unsigned char _rygel_start_uploading (dev_ctx_T *, char *, char *);
STATIC unsigned char _rygel_start_doing (dev_ctx_T *, char *, char *, State, char *);
STATIC unsigned char _rygel_find_av_transport (dev_ctx_T *);
STATIC void _rygel_device_cb (GUPnPControlPoint *, GUPnPDeviceProxy *, gpointer);
STATIC void _rygel_av_transport_cb (GUPnPControlPoint *, GUPnPDeviceProxy *, gpointer);
STATIC void _rygel_content_cb (GUPnPServiceProxy *, GUPnPServiceProxyAction *, gpointer);
STATIC void _rygel_metadata_cb (GUPnPServiceProxy *, GUPnPServiceProxyAction *, gpointer);
STATIC void _rygel_select_cb (GUPnPServiceProxy *, GUPnPServiceProxyAction *, gpointer);
STATIC void _rygel_upload_cb (GUPnPServiceProxy *, GUPnPServiceProxyAction *, gpointer);
STATIC void _rygel_transfer_cb (GUPnPServiceProxy *, GUPnPServiceProxyAction *, gpointer);
STATIC void _rygel_do_cb (GUPnPServiceProxy *, GUPnPServiceProxyAction *, gpointer);

static unsigned int client_count = 0;
static struct dev_ctx **dev_ctx = NULL;

#endif /* MEDIA_RYGEL_H */
