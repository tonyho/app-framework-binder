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

#include "local-def.h"

#define URN_MEDIA_SERVER "urn:schemas-upnp-org:device:MediaServer:1"
#define URN_CONTENT_DIR  "urn:schemas-upnp-org:service:ContentDirectory"

typedef struct dev_ctx dev_ctx_T;

struct dev_ctx {
    GMainContext *loop;
    GUPnPContext *context;
    GUPnPDeviceInfo *device_info;
    GUPnPServiceInfo *content_dir;
    char *content_res;
};

STATIC void _rygel_device_cb (GUPnPControlPoint *, GUPnPDeviceProxy *, gpointer);
STATIC void _rygel_content_cb (GUPnPServiceProxy *, GUPnPServiceProxyAction *, gpointer);

static gint handler_cb;
static unsigned int client_count = 0;
static struct dev_ctx **dev_ctx = NULL;

#endif /* MEDIA_RYGEL_H */
