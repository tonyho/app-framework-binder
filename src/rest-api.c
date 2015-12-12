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

#include "../include/local-def.h"

#include <setjmp.h>
#include <signal.h>

#define AFB_MSG_JTYPE "AJB_reply"


// handle to hold queryAll values
typedef struct {
     char    *msg;
     int     idx;
     size_t  len;
} queryHandleT;

static json_object     *afbJsonType;


// Sample Generic Ping Debug API
PUBLIC json_object* apiPingTest(AFB_request *request) {
    static pingcount = 0;
    json_object *response;
    char query [512];
    int len;

    // request all query key/value
    len = getQueryAll (request, query, sizeof(query));
    if (len == 0) strcpy (query,"NoSearchQueryList");
    
    // check if we have some post data
    if (request->post == NULL)  request->post="NoData";  
        
    // return response to caller
    response = jsonNewMessage(AFB_SUCCESS, "Ping Binder Daemon count=%d CtxtId=%d Loa=%d query={%s} PostData: \'%s\' "
               , pingcount++, request->client->cid, request->loa, query, request->post);
    return (response);
}

// Helper to retrieve argument from  connection
PUBLIC const char* getQueryValue(AFB_request * request, char *name) {
    const char *value;

    value = MHD_lookup_connection_value(request->connection, MHD_GET_ARGUMENT_KIND, name);
    return (value);
}

STATIC int getQueryCB (void*handle, enum MHD_ValueKind kind, const char *key, const char *value) {
    queryHandleT *query = (queryHandleT*)handle;
        
    query->idx += snprintf (&query->msg[query->idx],query->len," %s: \'%s\',", key, value);
}

// Helper to retrieve argument from  connection
PUBLIC int getQueryAll(AFB_request * request, char *buffer, size_t len) {
    queryHandleT query;
    buffer[0] = '\0'; // start with an empty string
    query.msg= buffer;
    query.len= len;
    query.idx= 0;

    MHD_get_connection_values (request->connection, MHD_GET_ARGUMENT_KIND, getQueryCB, &query);
    return (len);
}

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

// Check of apiurl is declare in this plugin and call it
STATIC AFB_error callPluginApi(AFB_plugin *plugin, AFB_request *request) {
    json_object *jresp, *jcall;
    int idx, status, sig;
    int signals[]= {SIGALRM, SIGSEGV, SIGFPE, 0};
    
    /*---------------------------------------------------------------
    | Signal handler defined inside CallPluginApi to access Request
    +---------------------------------------------------------------- */
    void pluginError (int signum) {
      sigset_t sigset;
      AFB_clientCtx *context;
              
      // unlock signal to allow a new signal to come
      sigemptyset (&sigset);
      sigaddset   (&sigset, signum);
      sigprocmask (SIG_UNBLOCK, &sigset, 0);

      fprintf (stderr, "Oops:%s Plugin Api Timeout timeout\n", configTime());
      longjmp (request->checkPluginCall, signum);
    }

    
    // If a plugin hold this urlpath call its callback
    for (idx = 0; plugin->apis[idx].callback != NULL; idx++) {
        if (!strcmp(plugin->apis[idx].name, request->api)) {
            
            // prepare an object to store calling values
            jcall=json_object_new_object();
            json_object_object_add(jcall, "prefix", json_object_new_string (plugin->prefix));
            json_object_object_add(jcall, "api"   , json_object_new_string (plugin->apis[idx].name));
            
            // save context before calling the API
            status = setjmp (request->checkPluginCall);
            if (status != 0) {    
                
                // Plugin aborted somewhere during its execution
                json_object_object_add(jcall, "status", json_object_new_string ("abort"));
                json_object_object_add(jcall, "info" ,  json_object_new_string ("Plugin broke during execution"));
                json_object_object_add(request->jresp, "request", jcall);
                
            } else {
                
                // If timeout protection==0 we are in debug and we do not apply signal protection
                if (request->config->apiTimeout > 0) {
                    for (sig=0; signals[sig] != 0; sig++) {
                       if (signal (signals[sig], pluginError) == SIG_ERR) {
                           request->errcode = MHD_HTTP_UNPROCESSABLE_ENTITY;
                           fprintf (stderr, "%s ERR: main no Signal/timeout handler installed.", configTime());
                           return AFB_FAIL;
                       }
                    }
                    // Trigger a timer to protect from unacceptable long time execution
                    alarm (request->config->apiTimeout);
                }
                
                // add client context to request
                ctxClientGet(request);      
                
                // Effectively call the API with a subset of the context
                jresp = plugin->apis[idx].callback(request);

                // API should return NULL of a valid Json Object
                if (jresp == NULL) {
                    json_object_object_add(jcall, "status", json_object_new_string ("null"));
                    json_object_object_add(request->jresp, "request", jcall);
                    request->errcode = MHD_HTTP_NO_RESPONSE;
                    
                } else {
                    json_object_object_add(jcall, "status", json_object_new_string ("processed"));
                    json_object_object_add(request->jresp, "request", jcall);
                    json_object_object_add(request->jresp, "response", jresp);
                }
                // cancel timeout and plugin signal handle before next call
                if (request->config->apiTimeout > 0) {
                    alarm (0);
                    for (sig=0; signals[sig] != 0; sig++) {
                       signal (signals[sig], SIG_DFL);
                    }
                }              
            }       
            return (AFB_DONE);
        }
    }
    return (AFB_FAIL);
}


// process rest API query
PUBLIC int doRestApi(struct MHD_Connection *connection, AFB_session *session, const char* url, const char *method
    , const char *upload_data, size_t *upload_data_size, void **con_cls) {
    
    static int postcount = 0; // static counter to debug POST protocol
    char *baseurl, *baseapi, *urlcpy1, *urlcpy2, *query, *token, *uuid;
    json_object *errMessage;
    AFB_error status;
    struct MHD_Response *webResponse;
    const char *serialized, parsedurl;
    AFB_request request;
    AFB_HttpPost *posthandle = *con_cls;
    AFB_clientCtx clientCtx;
    int idx, ret;

    // Extract plugin urlpath from request and make two copy because strsep overload copy
    urlcpy1 = urlcpy2 = strdup(url);
    baseurl = strsep(&urlcpy2, "/");
    if (baseurl == NULL) {
        errMessage = jsonNewMessage(AFB_FATAL, "Invalid API call url=[%s]", url);
        goto ExitOnError;
    }

    baseapi = strsep(&urlcpy2, "/");
    if (baseapi == NULL) {
        errMessage = jsonNewMessage(AFB_FATAL, "Invalid API call url=[%s]", url);
        goto ExitOnError;
    }
    

    // if post data may come in multiple calls
    if (0 == strcmp(method, MHD_HTTP_METHOD_POST)) {
        const char *encoding, *param;
        int contentlen = -1;
        AFB_HttpPost *posthandle = *con_cls;

        // Let make sure we have the right encoding and a valid length
        encoding = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_CONTENT_TYPE);
        param = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_CONTENT_LENGTH);
        if (param) sscanf(param, "%i", &contentlen);

        // POST datas may come in multiple chunk. Even when it never happen on AFB, we still have to handle the case
        if (strcasestr(encoding, JSON_CONTENT) == 0) {
            errMessage = jsonNewMessage(AFB_FATAL, "Post Date wrong type encoding=%s != %s", encoding, JSON_CONTENT);
            goto ExitOnError;
        }

        if (contentlen > MAX_POST_SIZE) {
            errMessage = jsonNewMessage(AFB_FATAL, "Post Date to big %d > %d", contentlen, MAX_POST_SIZE);
            goto ExitOnError;
        }

        // In POST mode first libmicrohttp call only establishes POST handling.
        if (posthandle == NULL) {
            posthandle = malloc(sizeof (AFB_HttpPost)); // allocate application POST processor handle
            posthandle->uid = postcount++; // build a UID for DEBUG
            posthandle->len = 0; // effective length within POST handler
            posthandle->data = malloc(contentlen + 1); // allocate memory for full POST data + 1 for '\0' enf of string
            *con_cls = posthandle; // attache POST handle to current HTTP session

            if (verbose) fprintf(stderr, "Create Post[%d] Size=%d\n", posthandle->uid, contentlen);
            return MHD_YES;
        }

        // This time we receive partial/all Post data. Note that even if we get all POST data. We should nevertheless
        // return MHD_YES and not process the request directly. Otherwise Libmicrohttpd is unhappy and fails with
        // 'Internal application error, closing connection'.
        if (*upload_data_size) {
            if (verbose) fprintf(stderr, "Update Post[%d]\n", posthandle->uid);

            memcpy(&posthandle->data[posthandle->len], upload_data, *upload_data_size);
            posthandle->len = posthandle->len + *upload_data_size;
            *upload_data_size = 0;
            return MHD_YES;
        }

        // We should only start to process DATA after Libmicrohttpd call or application handler with *upload_data_size==0
        // At this level we're may verify that we got everything and process DATA
        if (posthandle->len != contentlen) {
            errMessage = jsonNewMessage(AFB_FATAL, "Post Data Incomplete UID=%d Len %d != %s", posthandle->uid, contentlen, posthandle->len);
            goto ExitOnError;
        }

        // Before processing data, make sure buffer string is properly ended
        posthandle->data[posthandle->len] = '\0';
        request.post = posthandle->data;

        if (verbose) fprintf(stderr, "Close Post[%d] Buffer=%s\n", posthandle->uid, request.post);

    } else {
        request.post = NULL;
    };
        
    // build request structure
    memset(&request, 0, sizeof (request));
    request.connection = connection;
    request.config     = session->config;
    request.url = url;
    request.plugin = baseurl;
    request.api = baseapi;
    request.jresp = json_object_new_object();
    
    // increase reference count and add jtype to response    
    json_object_get (afbJsonType);
    json_object_object_add (request.jresp, "jtype", afbJsonType);
    
    // Search for a plugin with this urlpath
    for (idx = 0; session->plugins[idx] != NULL; idx++) {
        if (!strcmp(session->plugins[idx]->prefix, baseurl)) {
            status =callPluginApi(session->plugins[idx], &request);
            break;
        }
    }
    // No plugin was found
    if (session->plugins[idx] == NULL) {
        errMessage = jsonNewMessage(AFB_FATAL, "No Plugin=[%s]", request.plugin);
        goto ExitOnError;
    }

    // plugin callback did not return a valid Json Object
    if (status != AFB_DONE) {
        errMessage = jsonNewMessage(AFB_FATAL, "No API=[%s] for Plugin=[%s]", request.api, request.plugin);
        goto ExitOnError;
    }

    serialized = json_object_to_json_string(request.jresp);
    webResponse = MHD_create_response_from_buffer(strlen(serialized), (void*) serialized, MHD_RESPMEM_MUST_COPY);
    free(urlcpy1);
    
    // client did not pass token on URI let's use cookies 
    if (!request.restfull) {
       char cookie[64]; 
       snprintf (cookie, sizeof (cookie), "%s=%s", COOKIE_NAME,  request.client->uuid); 
       MHD_add_response_header (webResponse, MHD_HTTP_HEADER_SET_COOKIE, cookie);
       // if(verbose) fprintf(stderr,"Cookie: [%s]\n", cookie);
    }
    
    // if requested add an error status
    if (request.errcode != 0)  ret=MHD_queue_response (connection, request.errcode, webResponse);
    else ret = MHD_queue_response(connection, MHD_HTTP_OK, webResponse);
    
    MHD_destroy_response(webResponse);
    json_object_put(request.jresp); // decrease reference rqtcount to free the json object
    return ret;

ExitOnError:
    free(urlcpy1);
    serialized = json_object_to_json_string(errMessage);
    webResponse = MHD_create_response_from_buffer(strlen(serialized), (void*) serialized, MHD_RESPMEM_MUST_COPY);
    ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, webResponse);
    MHD_destroy_response(webResponse);
    json_object_put(errMessage); // decrease reference rqtcount to free the json object
    return ret;
}


// Loop on plugins. Check that they have the right type, prepare a JSON object with prefix
STATIC AFB_plugin ** RegisterPlugins(AFB_plugin **plugins) {
    int idx, jdx;

    for (idx = 0; plugins[idx] != NULL; idx++) {
        if (plugins[idx]->type != AFB_PLUGIN) {
            fprintf(stderr, "ERROR: AFSV plugin[%d] invalid type=%d != %d\n", idx, AFB_PLUGIN, plugins[idx]->type);
        } else {
            // some sanity controls
            if ((plugins[idx]->prefix == NULL) || (plugins[idx]->info == NULL) || (plugins[idx]->apis == NULL)) {
                if (plugins[idx]->prefix == NULL) plugins[idx]->prefix = "No URL prefix for APIs";
                if (plugins[idx]->info == NULL) plugins[idx]->info = "No Info describing plugin APIs";
                fprintf(stderr, "ERROR: plugin[%d] invalid prefix=%s info=%s", idx, plugins[idx]->prefix, plugins[idx]->info);
                return NULL;
            }

            if (verbose) fprintf(stderr, "Loading plugin[%d] prefix=[%s] info=%s\n", idx, plugins[idx]->prefix, plugins[idx]->info);
            
            // register private to plugin global context [cid=0]
            plugins[idx]->ctxGlobal= malloc (sizeof (AFB_clientCtx));
            plugins[idx]->ctxGlobal->cid=0;

            // Prebuild plugin jtype to boost API response
            plugins[idx]->jtype = json_object_new_string(plugins[idx]->prefix);
            json_object_get(plugins[idx]->jtype); // increase reference count to make it permanent
            plugins[idx]->prefixlen = strlen(plugins[idx]->prefix);
            
              
            // Prebuild each API jtype to boost API json response
            for (jdx = 0; plugins[idx]->apis[jdx].name != NULL; jdx++) {
                AFB_privateApi *private = malloc (sizeof (AFB_privateApi));
                if (plugins[idx]->apis[jdx].private != NULL) {
                    fprintf (stderr, "WARNING: plugin=%s api=%s private handle should be NULL\n"
                            ,plugins[idx]->prefix,plugins[idx]->apis[jdx].name);
                }
                private->len = strlen (plugins[idx]->apis[jdx].name);
                private->jtype=json_object_new_string(plugins[idx]->apis[jdx].name);
                json_object_get(private->jtype); // increase reference count to make it permanent
                plugins[idx]->apis[jdx].private = private;
            }
        }
    }
    return (plugins);
}

void initPlugins(AFB_session *session) {
    static AFB_plugin * plugins[10];
    afbJsonType = json_object_new_string (AFB_MSG_JTYPE);
    int i = 0;

    plugins[i++] = afsvRegister(session),
    plugins[i++] = dbusRegister(session),
    plugins[i++] = alsaRegister(session),
#ifdef HAVE_RADIO_PLUGIN
    plugins[i++] = radioRegister(session),
#endif
    plugins[i++] = NULL;
    
    // complete plugins and save them within current sessions    
    session->plugins = RegisterPlugins(plugins);
}
