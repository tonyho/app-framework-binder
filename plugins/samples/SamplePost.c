/*
 * Copyright (C) 2015 "IoT.bzh"
 * Author "Fulup Ar Foll"
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

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <json.h>

#include "afb-plugin.h"
#include "afb-req-itf.h"


static int fillargs(json_object *args, struct afb_arg arg)
{
    json_object *obj;

    obj = json_object_new_object();
    json_object_object_add (obj, "value", json_object_new_string(arg.value));
    if (arg.path != NULL)
	json_object_object_add (obj, "path", json_object_new_string(arg.path));
    json_object_object_add (obj, "size", json_object_new_int64((int64_t)arg.size));
    json_object_object_add (args, arg.name && *arg.name ? arg.name : "<empty-string>", obj);
    return 1; /* continue to iterate */
}

// Sample Generic Ping Debug API
static void getPingTest(struct afb_req request)
{
    static int pingcount = 0;
    json_object *query;

    query = json_object_new_object();
    afb_req_iterate(request, (void*)fillargs, query);

    afb_req_success_f(request, query, "Ping Binder Daemon count=%d", ++pingcount);
}

// With content-type=json data are directly avaliable in request->post->data
static void GetJsonByPost (struct afb_req request)
{
    json_object* jresp;
    json_object *query;
    struct afb_arg arg;

    query = json_object_new_object();
    afb_req_iterate(request, (void*)fillargs, query);

    arg = afb_req_get(request, "");
    jresp = arg.value ? json_tokener_parse(arg.value) : NULL;
    afb_req_success_f(request, jresp, "GetJsonByPost query={%s}", json_object_to_json_string(query));
}

// Upload a file and execute a function when upload is done
static void Uploads (struct afb_req request, const char *destination)
{
   afb_req_fail_f(request, "unimplemented", "destination: %s", destination);
}

// Upload a file and execute a function when upload is done
static void UploadAppli (struct afb_req request)
{
    Uploads(request, "applications");
}

// Simples Upload case just upload a file
static void UploadMusic (struct afb_req request)
{
    Uploads(request, "musics");
}

// PostForm callback is called multiple times (one or each key within form, or once per file buffer)
// When file has been fully uploaded call is call with item==NULL 
static void UploadImage (struct afb_req request)
{
    Uploads(request, "images");
}


// NOTE: this sample does not use session to keep test a basic as possible
//       in real application upload-xxx should be protected with AFB_SESSION_CHECK
static const struct AFB_restapi pluginApis[]= {
  {"ping"         , AFB_SESSION_NONE  , getPingTest    ,"Ping Rest Test Service"},
  {"upload-json"  , AFB_SESSION_NONE  , GetJsonByPost  ,"Demo for Json Buffer on Post"},
  {"upload-image" , AFB_SESSION_NONE  , UploadImage    ,"Demo for file upload"},
  {"upload-music" , AFB_SESSION_NONE  , UploadMusic    ,"Demo for file upload"},
  {"upload-appli" , AFB_SESSION_NONE  , UploadAppli    ,"Demo for file upload"},
  {NULL}
};

static const struct AFB_plugin plugin_desc = {
	.type = AFB_PLUGIN_JSON,
	.info = "Sample with Post Upload Files",
	.prefix = "post",
	.apis = pluginApis
};

const struct AFB_plugin *pluginRegister (const struct AFB_interface *itf)
{
    return &plugin_desc;
};
