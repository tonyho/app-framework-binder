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

/* -------------- MEDIA RYGEL IMPLEMENTATION ---------------- */

/* --- PUBLIC FUNCTIONS --- */

PUBLIC unsigned char _rygel_init (mediaCtxHandleT *ctx) {

    GMainContext *loop;
    GUPnPContext *context;
    GUPnPControlPoint *control_point;
    struct timeval tv_start, tv_now;

    context = gupnp_context_new (NULL, NULL, 0, NULL);

    control_point = gupnp_control_point_new (context, URN_MEDIA_SERVER);

    g_signal_connect (control_point, "device-proxy-available",
                      G_CALLBACK (_rygel_device_cb), ctx);

    /* start searching for servers */
    gssdp_resource_browser_set_active (GSSDP_RESOURCE_BROWSER (control_point), TRUE);

    loop = g_main_context_default ();

    /* 5 seconds should be sufficient to find Rygel */
    gettimeofday (&tv_start, NULL);
    gettimeofday (&tv_now, NULL);
    while (tv_now.tv_sec - tv_start.tv_sec <= 5) {

        g_main_context_iteration (loop, FALSE);

        if (ctx->media_server)
            break;
    }
    /* fail if we found no server */
    if (!ctx->media_server)
      return -1;

    dev_ctx[client_count]->loop = loop;
    dev_ctx[client_count]->context = context;

    client_count++;

    return 0;
}

PUBLIC void _rygel_free (mediaCtxHandleT *ctx) {

    dev_ctx_T *dev_ctx_c = (dev_ctx_T*)ctx->media_server;

    client_count--;

    g_main_context_unref (dev_ctx_c->loop);
    dev_ctx_c->loop = NULL;
    dev_ctx_c->context = NULL;
    dev_ctx_c->device_info = NULL;
    dev_ctx_c->content_dir = NULL;
}

PUBLIC char* _rygel_list (mediaCtxHandleT *ctx) {

    dev_ctx_T *dev_ctx_c = (dev_ctx_T*)ctx->media_server;
    GUPnPServiceProxy *content_dir_proxy;

    dev_ctx_c->content_res = NULL;
    content_dir_proxy = GUPNP_SERVICE_PROXY (dev_ctx_c->content_dir);

    gupnp_service_proxy_begin_action (content_dir_proxy, "Browse", _rygel_content_cb, dev_ctx_c,
                                      "ObjectID", G_TYPE_STRING, "Filesystem",
                                      "BrowseFlag", G_TYPE_STRING, "BrowseDirectChildren",
                                      "Filter", G_TYPE_STRING, "@childCount",
                                      "StartingIndex", G_TYPE_UINT, 0,
                                      "RequestedCount", G_TYPE_UINT, 64,
                                      "SortCriteria", G_TYPE_STRING, "",
                                       NULL);

    while (!dev_ctx_c->content_res)
      g_main_context_iteration (dev_ctx_c->loop, FALSE);

    return dev_ctx_c->content_res;
}

 /* ---- LOCAL CALLBACK FUNCTIONS ---- */

STATIC void _rygel_device_cb (GUPnPControlPoint *point, GUPnPDeviceProxy *proxy,
                                     gpointer data) {

    mediaCtxHandleT *ctx = (mediaCtxHandleT*)data;
    GUPnPDeviceInfo *device_info;
    GUPnPServiceInfo *content_dir;
    const char *device_name;

    device_info = GUPNP_DEVICE_INFO (proxy);
    device_name = gupnp_device_info_get_model_name (device_info);
    content_dir = gupnp_device_info_get_service (device_info, URN_CONTENT_DIR);

    if (strcmp (device_name, "Rygel") != 0)
        return;
    if (!content_dir)
        return;

    /* allocate the global array if it has not been not done */
    if (!dev_ctx)
        dev_ctx = (dev_ctx_T**) malloc (sizeof(dev_ctx_T));
    else
        dev_ctx = (dev_ctx_T**) realloc (dev_ctx, (client_count+1)*sizeof(dev_ctx_T));

    /* create an element for the client in the global array */
    dev_ctx[client_count] = malloc (sizeof(dev_ctx_T));
    dev_ctx[client_count]->device_info = device_info;
    dev_ctx[client_count]->content_dir = content_dir;

    /* make the client context aware of it */
    ctx->media_server = (void*)dev_ctx[client_count];
}

STATIC void _rygel_content_cb (GUPnPServiceProxy *content_dir, GUPnPServiceProxyAction *action,
                                      gpointer data) {

    dev_ctx_T *dev_ctx_c = (dev_ctx_T*)data;
    GUPnPServiceProxy *content_dir_proxy = GUPNP_SERVICE_PROXY (content_dir);
    GError *error;
    char *result;
    guint32 number_returned;
    guint32 total_matches;
    char *found;
    char subid[33];

    gupnp_service_proxy_end_action (content_dir, action, &error,
                                    "Result", G_TYPE_STRING, &result,
                                    "NumberReturned", G_TYPE_UINT, &number_returned,
                                    "TotalMatches", G_TYPE_UINT, &total_matches,
                                     NULL);

    if (number_returned == 0)
        return;

    if (number_returned == 1) {
        found = strstr (result, "id=\"");	
        found += 4;
        strncpy (subid, found, 32); subid[32] = '\0';

	gupnp_service_proxy_begin_action (content_dir_proxy, "Browse", _rygel_content_cb, NULL,
					  "ObjectID", G_TYPE_STRING, subid,
					  "BrowseFlag", G_TYPE_STRING, "BrowseDirectChildren",
					  "Filter", G_TYPE_STRING, "@childCount",
					  "StartingIndex", G_TYPE_UINT, 0,
					  "RequestedCount", G_TYPE_UINT, 64,
					  "SortCriteria", G_TYPE_STRING, "",
					   NULL);
        return;
    }

    if (number_returned > 1)
        dev_ctx_c->content_res = strdup (result);
}
