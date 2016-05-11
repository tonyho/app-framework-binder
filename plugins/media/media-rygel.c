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
#include <stdio.h>
#include <string.h>

#include "media-api.h"
#include "media-rygel.h"

/* -------------- MEDIA RYGEL IMPLEMENTATION ---------------- */

/* --- PUBLIC FUNCTIONS --- */

unsigned char _rygel_init (mediaCtxHandleT *ctx) {

    GMainContext *loop;
    GUPnPContext *context;
    GUPnPControlPoint *control_point;
    gint handler_cb;
    struct timeval tv_start, tv_now;

    context = gupnp_context_new (NULL, NULL, 0, NULL);

    control_point = gupnp_control_point_new (context, URN_MEDIA_SERVER);

    handler_cb = g_signal_connect (control_point, "device-proxy-available",
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
        gettimeofday (&tv_now, NULL);
    }
    /* fail if we found no server */
    if (!ctx->media_server)
      return 0;

    /* we have found the server ; stop looking for it... */
    g_signal_handler_disconnect (control_point, handler_cb);

    dev_ctx[client_count]->loop = loop;
    dev_ctx[client_count]->context = context;
    dev_ctx[client_count]->av_transport = NULL;
    dev_ctx[client_count]->state = STOP;
    dev_ctx[client_count]->target_state = STOP;
    dev_ctx[client_count]->action_args = NULL;
    dev_ctx[client_count]->transfer_started = 0;

    client_count++;

    return 1;
}

void _rygel_free (mediaCtxHandleT *ctx) {

    dev_ctx_T *dev_ctx_c = (dev_ctx_T*)ctx->media_server;

    client_count--;

    g_main_context_unref (dev_ctx_c->loop);
    dev_ctx_c->loop = NULL;
    dev_ctx_c->context = NULL;
    dev_ctx_c->device_info = NULL;
    dev_ctx_c->av_transport = NULL;
    dev_ctx_c->content_dir = NULL;
    dev_ctx_c->content_res = NULL;
}

json_object* _rygel_list (mediaCtxHandleT *ctx) {

    dev_ctx_T *dev_ctx_c = (dev_ctx_T*)ctx->media_server;
    json_object *json_o, *json_a;
    char *raw, *start, *end, *id, *title;
    int length, i = 0;

    if (!dev_ctx_c)
      return NULL;

    raw = _rygel_list_raw (dev_ctx_c, NULL);
    if (!raw)
      return NULL;

    start = strstr (raw, "<dc:title>");
    if (!start)
      return NULL;

    json_o = json_object_new_object ();
    json_a = json_object_new_array ();
    while (start) {
        json_object *json_i, *json_id, *json_title;

        start = strstr (start, "<dc:title>");
        if (!start) break;
        end = strstr (start, "</dc:title>");
        start += 10;
        length = end - start;

        asprintf (&id, "%02d", i);

        title = (char*) malloc (length+1);
        strncpy (title, start, length);
        title[length] = '\0';

        json_i = json_object_new_object ();
        json_id = json_object_new_string (id);
        json_title = json_object_new_string (title);
        json_object_object_add (json_i, "id", json_id);
        json_object_object_add (json_i, "title", json_title);
        json_object_array_add (json_a, json_i);

        free (id); free (title); 
        i++;
    }

    json_object_object_add (json_o, "list", json_a);

    return json_o;
}

unsigned char _rygel_select (mediaCtxHandleT *ctx, unsigned int index) {

    dev_ctx_T *dev_ctx_c = (dev_ctx_T*)ctx->media_server;
    unsigned int count;

    if (!dev_ctx_c)
      return 0;

    if (!_rygel_list_raw (dev_ctx_c, &count) ||
        index >= count)
      return 0;

    if (ctx->index != index)
      dev_ctx_c->state = STOP;

    return 1;
}

unsigned char _rygel_upload (mediaCtxHandleT *ctx, char *path) {

    dev_ctx_T *dev_ctx_c = (dev_ctx_T*)ctx->media_server;
    char *raw, *upload_id;

    if (!dev_ctx_c)
      return 0;

    raw = _rygel_list_raw (dev_ctx_c, NULL);
    if (!raw)
      return 0;

    /* for now, we always use the same upload container id */
    upload_id = _rygel_find_upload_id (dev_ctx_c, raw);

    return _rygel_start_uploading (dev_ctx_c, path, upload_id);
}

unsigned char _rygel_do (mediaCtxHandleT *ctx, State state, char *args) {

    dev_ctx_T *dev_ctx_c = (dev_ctx_T*)ctx->media_server;
    unsigned int index = ctx->index;
    unsigned int count;
    char *raw, *id, *metadata, *uri;

    if (!dev_ctx_c || dev_ctx_c->state == state)
        return 0;

    raw = _rygel_list_raw (dev_ctx_c, &count);
    if (!raw || index >= count)
      return 0;

          id = _rygel_find_id_for_index (dev_ctx_c, raw, index);
    metadata = _rygel_find_metadata_for_id (dev_ctx_c, id);
         uri = _rygel_find_uri_for_metadata (dev_ctx_c, metadata);

    return _rygel_start_doing (dev_ctx_c, uri, metadata, state, args);
}

/* --- LOCAL HELPER FUNCTIONS --- */

char* _rygel_list_raw (dev_ctx_T* dev_ctx_c, unsigned int *count) {

    GUPnPServiceProxy *content_dir_proxy;
    struct timeval tv_start, tv_now;

    dev_ctx_c->content_res = NULL;
    dev_ctx_c->content_num = 0;
    content_dir_proxy = GUPNP_SERVICE_PROXY (dev_ctx_c->content_dir);

    gupnp_service_proxy_begin_action (content_dir_proxy, "Browse", _rygel_content_cb, dev_ctx_c,
                                      "ObjectID", G_TYPE_STRING, "Filesystem",
                                      "BrowseFlag", G_TYPE_STRING, "BrowseDirectChildren",
                                      "Filter", G_TYPE_STRING, "@childCount",
                                      "StartingIndex", G_TYPE_UINT, 0,
                                      "RequestedCount", G_TYPE_UINT, 64,
                                      "SortCriteria", G_TYPE_STRING, "",
                                       NULL);

    gettimeofday (&tv_start, NULL);
    gettimeofday (&tv_now, NULL);
    while (tv_now.tv_sec - tv_start.tv_sec <= 5) {

        g_main_context_iteration (dev_ctx_c->loop, FALSE);

        if (dev_ctx_c->content_res)
            break;
        gettimeofday (&tv_now, NULL);
    }

    if (count) *count = dev_ctx_c->content_num;
    return dev_ctx_c->content_res;
}

char* _rygel_find_upload_id (dev_ctx_T* dev_ctx_c, char *raw) {

    char *found;
    char id[33];

    found = strstr (raw, "parentID=\"");
    found += 10;

    /* IDs are 32-bit strings */
    strncpy (id, found, 32);
    id[32] = '\0';

    return strdup (id);
}

char* _rygel_find_id_for_index (dev_ctx_T* dev_ctx_c, char *raw, unsigned int index) {

    char *found = raw;
    char id[33];
    int i;

    for (i = 0; i <= index; i++) {
        found = strstr (found, "item id=");
        found += 9;

        if (i == index) {
	    /* IDs are 32-bit strings */
            strncpy (id, found, 32);
            id[32] = '\0';
        }
    }

    return strdup (id);
}

char* _rygel_find_metadata_for_id (dev_ctx_T* dev_ctx_c, char *id) {

    GUPnPServiceProxy *content_dir_proxy;
    struct timeval tv_start, tv_now;

    dev_ctx_c->content_res = NULL;

    content_dir_proxy = GUPNP_SERVICE_PROXY (dev_ctx_c->content_dir);

    gupnp_service_proxy_begin_action (content_dir_proxy, "Browse", _rygel_metadata_cb, dev_ctx_c,
                                      "ObjectID", G_TYPE_STRING, id,
                                      "BrowseFlag", G_TYPE_STRING, "BrowseMetadata",
                                      "Filter", G_TYPE_STRING, "*",
                                      "StartingIndex", G_TYPE_UINT, 0,
                                      "RequestedCount", G_TYPE_UINT, 0,
                                      "SortCriteria", G_TYPE_STRING, "",
                                       NULL);

    gettimeofday (&tv_start, NULL);
    gettimeofday (&tv_now, NULL);
    while (tv_now.tv_sec - tv_start.tv_sec <= 5) {

        g_main_context_iteration (dev_ctx_c->loop, FALSE);

        if (dev_ctx_c->content_res)
            break;
        gettimeofday (&tv_now, NULL);
    }

    return dev_ctx_c->content_res;
}

char* _rygel_find_uri_for_metadata (dev_ctx_T* dev_ctx_c, char *metadata) {

    char *start, *end, *uri = NULL;
    int length;

    /* position ourselves after the first "<res " tag */
    start = strstr (metadata, "<res ");

    while (start) {
        start = strstr (start, "http://");
	if (!start) break;
	end = strstr (start, "</res>");
	length = end - start;


        uri = (char *)malloc (length + 1);
	strncpy (uri, start, length);
        uri[length] = '\0';
        /* if the URI contains "primary_http", it is the main one ; stop here...*/
        if (strstr (uri, "primary_http"))
          break;

        free (uri); start = end;
    }

    return uri;
}

char * _rygel_time_for_string (char *string) {

    int total_seconds;
    unsigned int hours, minutes, seconds;
    char *time;

    total_seconds = atoi (string);
    hours = total_seconds / 3600;
    minutes = (total_seconds / 60) - (hours * 60);
    seconds = total_seconds - (hours * 3600) - (minutes * 60);

    asprintf (&time, "%u:%02u:%02u", hours, minutes, seconds);

    return time;
}

unsigned char _rygel_start_uploading (dev_ctx_T* dev_ctx_c, char *path, char *upload_id) {

    GUPnPServiceProxy *content_dir_proxy;
    GUPnPDIDLLiteWriter *didl_writer;
    GUPnPDIDLLiteObject *didl_object;
    char *didl, *content_type, *mime_type, *upnp_class;
    struct timeval tv_start, tv_now;

    didl_writer = gupnp_didl_lite_writer_new (NULL);
    didl_object = GUPNP_DIDL_LITE_OBJECT (gupnp_didl_lite_writer_add_item (didl_writer));

    /* create the metadata for the file */
    gupnp_didl_lite_object_set_parent_id (didl_object, upload_id);
    gupnp_didl_lite_object_set_id (didl_object, "");
    gupnp_didl_lite_object_set_restricted (didl_object, FALSE);
    gupnp_didl_lite_object_set_title (didl_object, g_path_get_basename (path));
    /* deduce the UPnP class from the MIME type ("audio/ogg" e.g.) */
    content_type = g_content_type_guess (path, NULL, 0, NULL);
    mime_type = g_content_type_get_mime_type (content_type);
    if (strstr (mime_type, "audio/"))
      upnp_class = strdup ("object.item.audioItem.musicTrack");
    else if (strstr (mime_type, "video/"))
      upnp_class = strdup ("object.item.videoItem");
    else if (strstr (mime_type, "image/"))
      upnp_class = strdup ("object.item.imageItem");
    else
      upnp_class = strdup ("object.item");
    gupnp_didl_lite_object_set_upnp_class (didl_object, upnp_class);
    didl = gupnp_didl_lite_writer_get_string (didl_writer);

    dev_ctx_c->transfer_path = path;
    dev_ctx_c->transfer_started = 0;
    content_dir_proxy = GUPNP_SERVICE_PROXY (dev_ctx_c->content_dir);

    gupnp_service_proxy_begin_action (content_dir_proxy, "CreateObject", _rygel_upload_cb, dev_ctx_c,
                                      "ContainerID", G_TYPE_STRING, upload_id,
                                      "Elements", G_TYPE_STRING, didl,
                                       NULL);

    gettimeofday (&tv_start, NULL);
    gettimeofday (&tv_now, NULL);
    while (tv_now.tv_sec - tv_start.tv_sec <= 5) {

      g_main_context_iteration (dev_ctx_c->loop, FALSE);

      if (dev_ctx_c->transfer_started)
        break;
      gettimeofday (&tv_now, NULL);
    }
    if (!dev_ctx_c->transfer_started)
      return 0;

    return 1;
}

unsigned char _rygel_start_doing (dev_ctx_T* dev_ctx_c, char *uri, char *metadata, State state, char *args) {

    GUPnPServiceProxy *av_transport_proxy;
    struct timeval tv_start, tv_now;

    if (!dev_ctx_c->av_transport) {
      if (!_rygel_find_av_transport (dev_ctx_c))
         return 0;
    }
    dev_ctx_c->target_state = state;
    dev_ctx_c->action_args = args;
    av_transport_proxy = GUPNP_SERVICE_PROXY (dev_ctx_c->av_transport);

    gupnp_service_proxy_begin_action (av_transport_proxy, "SetAVTransportURI", _rygel_select_cb, dev_ctx_c,
                                      "InstanceID", G_TYPE_UINT, 0,
                                      "CurrentURI", G_TYPE_STRING, uri,
                                      "CurrentURIMetaData", G_TYPE_STRING, metadata,
                                       NULL);

    gettimeofday (&tv_start, NULL);
    gettimeofday (&tv_now, NULL);
    while (tv_now.tv_sec - tv_start.tv_sec <= 5) {

      g_main_context_iteration (dev_ctx_c->loop, FALSE);

      if (dev_ctx_c->state == state)
        break;
      gettimeofday (&tv_now, NULL);
    }
    if (dev_ctx_c->state != state)
      return 0;

    return 1;
}

unsigned char _rygel_find_av_transport (dev_ctx_T* dev_ctx_c) {

    GUPnPControlPoint *control_point;
    gint handler_cb;
    struct timeval tv_start, tv_now;

    control_point = gupnp_control_point_new (dev_ctx_c->context, URN_MEDIA_RENDERER);

    handler_cb = g_signal_connect (control_point, "device-proxy-available",
                                   G_CALLBACK (_rygel_av_transport_cb), dev_ctx_c);

    gssdp_resource_browser_set_active (GSSDP_RESOURCE_BROWSER (control_point), TRUE);

    gettimeofday (&tv_start, NULL);
    gettimeofday (&tv_now, NULL);
    while (tv_now.tv_sec - tv_start.tv_sec <= 5) {

        g_main_context_iteration (dev_ctx_c->loop, FALSE);

        if (dev_ctx_c->av_transport)
            break;
        gettimeofday (&tv_now, NULL);
    }
    g_signal_handler_disconnect (control_point, handler_cb);

    if (!dev_ctx_c->av_transport)
      return 0;

    return 1;
}


 /* ---- LOCAL CALLBACK FUNCTIONS ---- */

static void _rygel_device_cb (GUPnPControlPoint *point, GUPnPDeviceProxy *proxy,
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
    dev_ctx[client_count] = (dev_ctx_T*) malloc (sizeof(dev_ctx_T));
    dev_ctx[client_count]->device_info = device_info;
    dev_ctx[client_count]->content_dir = content_dir;

    /* make the client context aware of it */
    ctx->media_server = (void*)dev_ctx[client_count];
}

static void _rygel_av_transport_cb (GUPnPControlPoint *point, GUPnPDeviceProxy *proxy,
                                    gpointer data) {

    dev_ctx_T *dev_ctx_c = (dev_ctx_T*)data;
    GUPnPDeviceInfo *device_info;
    GUPnPServiceInfo *av_transport;

    device_info = GUPNP_DEVICE_INFO (proxy);
    av_transport = gupnp_device_info_get_service (device_info, URN_AV_TRANSPORT);

    dev_ctx_c->av_transport = av_transport;
}

static void _rygel_content_cb (GUPnPServiceProxy *content_dir, GUPnPServiceProxyAction *action,
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

	gupnp_service_proxy_begin_action (content_dir_proxy, "Browse", _rygel_content_cb, dev_ctx_c,
					  "ObjectID", G_TYPE_STRING, subid,
					  "BrowseFlag", G_TYPE_STRING, "BrowseDirectChildren",
					  "Filter", G_TYPE_STRING, "@childCount",
					  "StartingIndex", G_TYPE_UINT, 0,
					  "RequestedCount", G_TYPE_UINT, 64,
					  "SortCriteria", G_TYPE_STRING, "",
					   NULL);
        return;
    }

    if (number_returned > 1) {
        dev_ctx_c->content_res = result;
        dev_ctx_c->content_num = number_returned;
    }
}

static void _rygel_metadata_cb (GUPnPServiceProxy *content_dir, GUPnPServiceProxyAction *action,
                                gpointer data) {

    dev_ctx_T *dev_ctx_c = (dev_ctx_T*)data;
    GError *error;
    char *result;

    gupnp_service_proxy_end_action (content_dir, action, &error,
                                    "Result", G_TYPE_STRING, &result,
				     NULL);

    dev_ctx_c->content_res = result;
}

static void _rygel_select_cb (GUPnPServiceProxy *av_transport, GUPnPServiceProxyAction *action,
                              gpointer data)
{

    dev_ctx_T *dev_ctx_c = (dev_ctx_T*)data;
    GUPnPServiceProxy *av_transport_proxy;
    GError *error;
    char *time;
    struct timeval tv_start, tv_now;

    av_transport_proxy = GUPNP_SERVICE_PROXY (av_transport);

    gupnp_service_proxy_end_action (av_transport, action, &error, NULL);

    switch (dev_ctx_c->target_state) {
        case PLAY:
          gupnp_service_proxy_begin_action (av_transport_proxy, "Play", _rygel_do_cb, dev_ctx_c,
                                           "InstanceID", G_TYPE_UINT, 0,
                                           "Speed", G_TYPE_STRING, "1",
                                            NULL);
          break;
       case PAUSE:
          gupnp_service_proxy_begin_action (av_transport_proxy, "Pause", _rygel_do_cb, dev_ctx_c,
                                           "InstanceID", G_TYPE_UINT, 0,
                                            NULL);
          break;
       case STOP:
          gupnp_service_proxy_begin_action (av_transport_proxy, "Stop", _rygel_do_cb, dev_ctx_c,
                                           "InstanceID", G_TYPE_UINT, 0,
                                            NULL);
          break;
       case SEEK:
          time = _rygel_time_for_string (dev_ctx_c->action_args);
          gupnp_service_proxy_begin_action (av_transport_proxy, "Seek", _rygel_do_cb, dev_ctx_c,
                                           "InstanceID", G_TYPE_UINT, 0,
                                           "Unit", G_TYPE_STRING, "ABS_TIME",
                                           "Target", G_TYPE_STRING, time,
                                            NULL);
       default:
	 break;
    }

    gettimeofday (&tv_start, NULL);
    gettimeofday (&tv_now, NULL);
    while (tv_now.tv_sec - tv_start.tv_sec <= 5) {

        g_main_context_iteration (dev_ctx_c->loop, FALSE);

        if (dev_ctx_c->state == dev_ctx_c->target_state)
            break;
        gettimeofday (&tv_now, NULL);
    }
}

static void _rygel_upload_cb (GUPnPServiceProxy *content_dir, GUPnPServiceProxyAction *action,
                              gpointer data)
{
    dev_ctx_T *dev_ctx_c = (dev_ctx_T*)data;
    GUPnPServiceProxy *content_dir_proxy;
    GError *error;
    char *result, *start, *end, *dst_uri, *src_uri;
    int length;
    struct timeval tv_start, tv_now;

    content_dir_proxy = GUPNP_SERVICE_PROXY (content_dir);

    if (!gupnp_service_proxy_end_action (content_dir, action, &error,
                                         "Result", G_TYPE_STRING, &result,
                                          NULL))
      return;

    start = strstr (result, "<res importUri=\"");
    if (!start)
      return;

    start += 16;
    end = strstr (start, "\"");
    length = end - start;

    dst_uri = (char*) malloc(length+1);
    strncpy (dst_uri, start, length);
    dst_uri[length] = '\0';

    asprintf (&src_uri, "http://%s:%u%s", gupnp_context_get_host_ip (dev_ctx_c->context),
                                          gupnp_context_get_port (dev_ctx_c->context),
                                          dev_ctx_c->transfer_path);

    /* host the file */
    gupnp_context_host_path (dev_ctx_c->context, dev_ctx_c->transfer_path,
                                                 dev_ctx_c->transfer_path);

    gupnp_service_proxy_begin_action (content_dir_proxy, "ImportResource", _rygel_transfer_cb, dev_ctx_c,
                                      "SourceURI", G_TYPE_STRING, src_uri,
                                      "DestinationURI", G_TYPE_STRING, dst_uri,
                                       NULL);

    gettimeofday (&tv_start, NULL);
    gettimeofday (&tv_now, NULL);
    while (tv_now.tv_sec - tv_start.tv_sec <= 5) {

        g_main_context_iteration (dev_ctx_c->loop, FALSE);

        if (dev_ctx_c->transfer_started)
            break;
        gettimeofday (&tv_now, NULL);
    }
}

static void _rygel_transfer_cb (GUPnPServiceProxy *content_dir, GUPnPServiceProxyAction *action,
                                gpointer data)
{
    dev_ctx_T *dev_ctx_c = (dev_ctx_T*)data;
    GError *error;
    guint transfer_id;

    if (!gupnp_service_proxy_end_action (content_dir, action, &error,
                                         "TransferID", G_TYPE_UINT, &transfer_id,
                                          NULL))
      return;

    dev_ctx_c->transfer_started = 1;
}

static void _rygel_do_cb (GUPnPServiceProxy *av_transport, GUPnPServiceProxyAction *action,
                          gpointer data)
{
    dev_ctx_T *dev_ctx_c = (dev_ctx_T*)data;
    GError *error;

    if (!gupnp_service_proxy_end_action (av_transport, action, &error,
                                         NULL))
      return;

    dev_ctx_c->state = dev_ctx_c->target_state;
}
