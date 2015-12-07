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
 * 
 * Contain all generic part to handle REST/API
 */


#include <microhttpd.h>
#include <sys/stat.h>
#include "../include/local-def.h"

// proto missing from GCC
char *strcasestr(const char *haystack, const char *needle);


// Because of POST call multiple time requestApi we need to free POST handle here
STATIC void endRequest(void *cls, struct MHD_Connection *connection, void **con_cls, enum MHD_RequestTerminationCode toe) {
    AFB_HttpPost *posthandle = *con_cls;

    // if post handle was used let's free everything
    if (posthandle) {
        if (verbose) fprintf(stderr, "End Post Request UID=%d\n", posthandle->uid);
        free(posthandle->data);
        free(posthandle);
    }
}


PUBLIC json_object* pingSample (AFB_plugin *plugin, AFB_session *session, AFB_request *post) {
    static pingcount=0;
    json_object *response;
    response = jsonNewMessage(AFB_SUCCESS, "Ping Binder Daemon %d", pingcount++);
    if (verbose) fprintf(stderr, "%d: \n", pingcount);
    return (response);
}

// Check of apiurl is declare in this plugin and call it
STATIC json_object * callPluginApi (AFB_plugin *plugin, AFB_session *session,  AFB_request *request) {
    json_object *response;
    int idx;
    
    // If a plugin hold this urlpath call its callback
    for (idx=0; plugin->apis[idx].callback != NULL; idx++) {
        if (!strcmp (plugin->apis[idx].name, request->api)) {
           response = plugin->apis[idx].callback (session, request);
           if (response != NULL) {
               json_object_object_add (response, "jtype" ,plugin->jtype);
           }
           return (response);
        }   
    }
    return (NULL);
}


// process rest API query
PUBLIC int doRestApi(struct MHD_Connection *connection, AFB_session *session, const char *method, const char* url) {

    char *baseurl, *baseapi, *urlcpy;
    json_object *jsonResponse, *errMessage;
    struct MHD_Response *webResponse;
    const char *serialized, parsedurl;
    AFB_request request;
    int  idx, ret;  

    // Extract plugin urlpath from request
    urlcpy=strdup (url);
    baseurl = strsep(&urlcpy, "/");
    if (baseurl == NULL) {
        errMessage = jsonNewMessage(AFB_FATAL, "Invalid Plugin/API call url=%s", url);
        goto ExitOnError;
    }
    
    baseapi = strsep(&urlcpy, "/");
    if (baseapi == NULL) {
        errMessage = jsonNewMessage(AFB_FATAL, "Invalid Plugin/API call url=%s/%s", baseurl, url);
        goto ExitOnError;
    }
    
    // build request structure
    memset (&request, 0, sizeof (request));
    request.connection = connection;
    request.url        = url;
    request.plugin     = baseurl;
    request.api        = baseapi;

    // if post wait as data may come in multiple calls
    if (0 == strcmp (method, MHD_HTTP_METHOD_POST)) {
   
        request.post="TO Be DONE"; 
    } else {
        request.post=NULL;
    };
    
    // Search for a plugin with this urlpath
    for (idx=0; session->plugins[idx] != NULL; idx++) {
        if (!strcmp (session->plugins[idx]->prefix, baseurl)) {
           jsonResponse = callPluginApi (session->plugins[idx], session, &request );
           // free (urlcpy);
           break;
        }
    }
    // No plugin was found
    if (session->plugins[idx] == NULL) {
        errMessage = jsonNewMessage(AFB_FATAL, "No Plugin for %s", baseurl);
        free (urlcpy);
        goto ExitOnError;
    }

    // plugin callback did not return a valid Json Object
    if (jsonResponse == NULL) {
       errMessage = jsonNewMessage(AFB_FATAL, "No Plugin/API for %s/%s", baseurl, baseapi);
       goto ExitOnError;
    }

    serialized = json_object_to_json_string(jsonResponse);
    webResponse = MHD_create_response_from_buffer(strlen(serialized), (void*) serialized, MHD_RESPMEM_MUST_COPY);

    ret = MHD_queue_response(connection, MHD_HTTP_OK, webResponse);
    MHD_destroy_response(webResponse);
    json_object_put(jsonResponse); // decrease reference rqtcount to free the json object
    return ret;

ExitOnError:
    serialized = json_object_to_json_string(errMessage);
    webResponse = MHD_create_response_from_buffer(strlen(serialized), (void*) serialized, MHD_RESPMEM_MUST_COPY);
    ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, webResponse);
    MHD_destroy_response(webResponse);
    json_object_put(errMessage); // decrease reference rqtcount to free the json object
    return ret;
}

// Helper to retreive argument from  connection
PUBLIC const char* getQueryValue (AFB_request * request, char *name) {
    const char *value;
    
    value=MHD_lookup_connection_value(request->connection, MHD_GET_ARGUMENT_KIND, name);
    return (value);
}

// Loop on plugins. Check that they have the right type, prepare a JSON object with prefix
STATIC AFB_plugin ** RegisterPlugins(AFB_plugin **plugins) {
    int idx;
    
    for (idx=0; plugins[idx] != NULL; idx++) {
        if (plugins[idx]->type != AFB_PLUGIN) {
            fprintf (stderr, "ERROR: AFSV plugin[%d] invalid type=%d != %d\n", idx,  AFB_PLUGIN, plugins[idx]->type);
        } else {
            // some sanity controls
            if ((plugins[idx]->prefix == NULL) || (plugins[idx]->info == NULL) || (plugins[idx]->apis == NULL)){
                if (plugins[idx]->prefix == NULL) plugins[idx]->prefix = "No URL prefix for APIs";
                if (plugins[idx]->info == NULL) plugins[idx]->info = "No Info describing plugin APIs";
                fprintf (stderr, "ERROR: plugin[%d] invalid prefix=%s info=%s", idx,plugins[idx]->prefix, plugins[idx]->info);
                return NULL;
            }
            
            if (verbose) fprintf (stderr, "Loading plugin[%d] prefix=[%s] info=%s\n", idx, plugins[idx]->prefix, plugins[idx]->info);
            
            // Prepare Plugin name to be added into each API response
            plugins[idx]->jtype = json_object_new_string (plugins[idx]->prefix);
            json_object_get (plugins[idx]->jtype); // increase reference count to make it permanent
            
            // compute urlprefix lenght
            plugins[idx]->prefixlen = strlen (plugins[idx]->prefix);
        }  
    }
    return (plugins);
}

void initPlugins (AFB_session *session) {
    static AFB_plugin *plugins[10];

        plugins[0]= afsvRegister (session),
        plugins[1]= dbusRegister (session),
        plugins[2]= alsaRegister (session),
        plugins[3]= NULL;

    // complete plugins and save them within current sessions    
    session->plugins=  RegisterPlugins (plugins);  
}