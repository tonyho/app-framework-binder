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
