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
 * 
 *  https://www.gnu.org/software/libmicrohttpd/tutorial.html [search 'largepost.c']
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
    char query  [256];
    char session[256];

    int len;
    AFB_clientCtx *client=request->client; // get client context from request
    
    // request all query key/value
    len = getQueryAll (request, query, sizeof(query));
    if (len == 0) strncpy (query, "NoSearchQueryList", sizeof(query));
    
    // check if we have some post data
    if (request->post == NULL)  request->post->data="NoData"; 
    
    // check is we have a session and a plugin handle
    if (client == NULL) strcpy (session,"NoSession");       
    else snprintf(session, sizeof(session),"uuid=%s token=%s ctx=0x%x handle=0x%x", client->uuid, client->token, client->ctx, client->ctx); 
        
    // return response to caller
    response = jsonNewMessage(AFB_SUCCESS, "Ping Binder Daemon count=%d CtxtId=%d query={%s} session={%s} PostData: [%s] "
               , pingcount++, request->client->cid, query, session, request->post->data);
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


// Helper to retreive POST handle
PUBLIC AFB_PostHandle* getPostHandle (AFB_request *request) {
    if (request->post == NULL) return (NULL);
    return ((AFB_PostHandle*) request->post->data);
}

// Because of POST call multiple time requestApi we need to free POST handle here
// Note this method is called from http-svc just before closing session
PUBLIC void endPostRequest(AFB_PostHandle *postHandle) {

    if (postHandle->type == AFB_POST_JSON) {
        // if (verbose) fprintf(stderr, "End PostJson Request UID=%d\n", postHandle->uid);
    }

    if (postHandle->type == AFB_POST_FORM) {
         if (verbose) fprintf(stderr, "End PostForm Request UID=%d\n", postHandle->uid);
    }
    free(postHandle->private);
    free(postHandle);
}

// Check of apiurl is declare in this plugin and call it
STATIC AFB_error callPluginApi(AFB_plugin *plugin, AFB_request *request, void *context) {
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
            
            // Request was found and at least partially executed
            request->jresp  = json_object_new_object();
            json_object_get (afbJsonType);  // increate jsontype reference count
            json_object_object_add (request->jresp, "jtype", afbJsonType);
            
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
                            json_object_object_add(jcall, "status", json_object_new_string ("fail"));
                            json_object_object_add(jcall, "info", json_object_new_string ("Setting Timeout Handler Failed"));
                            json_object_object_add(request->jresp, "request", jcall);
                            return AFB_DONE;
                       }
                    }
                    // Trigger a timer to protect from unacceptable long time execution
                    alarm (request->config->apiTimeout);
                }

                // Out of SessionNone every call get a client context session
                if (AFB_SESSION_NONE != plugin->apis[idx].session) {
                    
                    // add client context to request
                    if (ctxClientGet(request, plugin) != AFB_SUCCESS) {
                        request->errcode=MHD_HTTP_INSUFFICIENT_STORAGE;
                        json_object_object_add(jcall, "status", json_object_new_string ("fail"));
                        json_object_object_add(jcall, "info", json_object_new_string ("Client Session Context Full !!!"));
                        json_object_object_add(request->jresp, "request", jcall);
                        return (AFB_DONE);                              
                    };
                    
                    if (verbose) fprintf(stderr, "Plugin=[%s] Api=[%s] Middleware=[%d] Client=[0x%x] Uuid=[%s] Token=[%s]\n"
                           , request->plugin, request->api, plugin->apis[idx].session, request->client, request->client->uuid, request->client->token);                        
                    
                    switch(plugin->apis[idx].session) {

                        case AFB_SESSION_CREATE:
                            if (request->client->token[0] != '\0') {
                                request->errcode=MHD_HTTP_UNAUTHORIZED;
                                json_object_object_add(jcall, "status", json_object_new_string ("exist"));
                                json_object_object_add(jcall, "info", json_object_new_string ("AFB_SESSION_CREATE Session already exist"));
                                json_object_object_add(request->jresp, "request", jcall);
                                return (AFB_DONE);                              
                            }
                        
                            if (AFB_SUCCESS != ctxTokenCreate (request)) {
                                request->errcode=MHD_HTTP_UNAUTHORIZED;
                                json_object_object_add(jcall, "status", json_object_new_string ("fail"));
                                json_object_object_add(jcall, "info", json_object_new_string ("AFB_SESSION_CREATE Invalid Initial Token"));
                                json_object_object_add(request->jresp, "request", jcall);
                                return (AFB_DONE);
                            } else {
                                json_object_object_add(jcall, "uuid", json_object_new_string (request->client->uuid));                                
                                json_object_object_add(jcall, "token", json_object_new_string (request->client->token));                                
                                json_object_object_add(jcall, "timeout", json_object_new_int (request->config->cntxTimeout));                                
                            }
                            break;


                        case AFB_SESSION_RENEW:
                            if (AFB_SUCCESS != ctxTokenRefresh (request)) {
                                request->errcode=MHD_HTTP_UNAUTHORIZED;
                                json_object_object_add(jcall, "status", json_object_new_string ("fail"));
                                json_object_object_add(jcall, "info", json_object_new_string ("AFB_SESSION_REFRESH Broken Exchange Token Chain"));
                                json_object_object_add(request->jresp, "request", jcall);
                                return (AFB_DONE);
                            } else {
                                json_object_object_add(jcall, "uuid", json_object_new_string (request->client->uuid));                                
                                json_object_object_add(jcall, "token", json_object_new_string (request->client->token));                                
                                json_object_object_add(jcall, "timeout", json_object_new_int (request->config->cntxTimeout));                                
                            }
                            break;

                        case AFB_SESSION_CLOSE:
                            if (AFB_SUCCESS != ctxTokenCheck (request)) {
                                request->errcode=MHD_HTTP_UNAUTHORIZED;
                                json_object_object_add(jcall, "status", json_object_new_string ("empty"));
                                json_object_object_add(jcall, "info", json_object_new_string ("AFB_SESSION_CLOSE Not a Valid Access Token"));
                                json_object_object_add(request->jresp, "request", jcall);
                                return (AFB_DONE);
                            } else {
                                json_object_object_add(jcall, "uuid", json_object_new_string (request->client->uuid));                                
                            }
                            break;
                        
                        case AFB_SESSION_CHECK:
                        default: 
                            // default action is check
                            if (AFB_SUCCESS != ctxTokenCheck (request)) {
                                request->errcode=MHD_HTTP_UNAUTHORIZED;
                                json_object_object_add(jcall, "status", json_object_new_string ("fail"));
                                json_object_object_add(jcall, "info", json_object_new_string ("AFB_SESSION_CHECK Invalid Active Token"));
                                json_object_object_add(request->jresp, "request", jcall);
                                return (AFB_DONE);
                            }
                            break;
                    }
                }
                
                // Effectively call the API with a subset of the context
                jresp = plugin->apis[idx].callback(request, context);
                
                // handle intemediatry Post Iterates out of band
                if ((jresp == NULL) && (request->errcode == MHD_HTTP_OK)) return (AFB_SUCCESS);

                // Session close is done after the API call so API can still use session in closing API
                if (AFB_SESSION_CLOSE == plugin->apis[idx].session) ctxTokenReset (request);                    
                
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

STATIC AFB_error findAndCallApi (AFB_request *request, void *context) {
    int idx;
    AFB_error status;
    
   
    // Search for a plugin with this urlpath
    for (idx = 0; request->plugins[idx] != NULL; idx++) {
        if (!strcmp(request->plugins[idx]->prefix, request->plugin)) {
            status =callPluginApi(request->plugins[idx], request, context);
            break;
        }
    }
    // No plugin was found
    if (request->plugins[idx] == NULL) {
        request->jresp = jsonNewMessage(AFB_FATAL, "No Plugin=[%s]", request->plugin);
        goto ExitOnError;
    }  
    
    // plugin callback did not return a valid Json Object
    if (status == AFB_FAIL) {
        request->jresp = jsonNewMessage(AFB_FATAL, "No API=[%s] for Plugin=[%s]", request->api, request->plugin);
        goto ExitOnError;
    }
    
    // Everything look OK
    return (status);
    
ExitOnError:
    request->errcode = MHD_HTTP_UNPROCESSABLE_ENTITY;
    return (AFB_FAIL);
}

// This CB is call for every item with a form post it reformat iterator values
// and callback Plugin API for each Item within PostForm.
doPostIterate (void *cls, enum MHD_ValueKind kind, const char *key,
              const char *filename, const char *mimetype,
              const char *encoding, const char *data, uint64_t offset,
              size_t size) {
  
  AFB_error    status;
  AFB_PostItem item;
    
  // retrieve API request from Post iterator handle  
  AFB_PostHandle *postHandle  = (AFB_PostHandle*)cls;
  AFB_request *request = (AFB_request*)postHandle->private;
  AFB_PostRequest postRequest;
  
  fprintf (stderr, "postHandle key=%s filename=%s len=%d mime=%s\n", key, filename, size, mimetype);
   
  // Create and Item value for Plugin API
  item.kind     = kind;
  item.key      = key;
  item.filename = filename;
  item.mimetype = mimetype;
  item.encoding = encoding;
  item.len      = size;
  item.data     = data;
  item.offset   = offset;
  
  // Reformat Request to make it somehow similar to GET/PostJson case
  postRequest.data= (char*) postHandle;
  postRequest.len = size;
  postRequest.type= AFB_POST_FORM;;
  request->post = &postRequest;
  
  // effectively call plugin API                 
  status = findAndCallApi (request, &item);
  // when returning no processing of postform stop
  if (status != AFB_SUCCESS) return MHD_NO;
  
  // let's allow iterator to move to next item
  return (MHD_YES);
}

STATIC void freeRequest (AFB_request *request) {

 free (request->plugin);    
 free (request->api);    
 free (request);    
}

STATIC AFB_request *createRequest (struct MHD_Connection *connection, AFB_session *session, const char* url) {
    
    AFB_request *request;
    
    // Start with a clean request
    request = calloc (1, sizeof (AFB_request));
    char *urlcpy1, *urlcpy2;
    char *baseapi, *baseurl;  
      
    // Extract plugin urlpath from request and make two copy because strsep overload copy
    urlcpy1 = urlcpy2 = strdup(url);
    baseurl = strsep(&urlcpy2, "/");
    if (baseurl == NULL) {
        request->jresp = jsonNewMessage(AFB_FATAL, "Invalid API call url=[%s]", url);
    }

    // let's compute URL and call API
    baseapi = strsep(&urlcpy2, "/");
    if (baseapi == NULL) {
        request->jresp = jsonNewMessage(AFB_FATAL, "Invalid API call url=[%s]", url);
    }
    
    // build request structure
    request->connection = connection;
    request->config = session->config;
    request->url    = url;
    request->plugin = strdup (baseurl);
    request->api    = strdup (baseapi);
    request->plugins= session->plugins;
    
    free(urlcpy1);
    return (request);
}

// process rest API query
PUBLIC int doRestApi(struct MHD_Connection *connection, AFB_session *session, const char* url, const char *method
    , const char *upload_data, size_t *upload_data_size, void **con_cls) {
    
    static int postcount = 0; // static counter to debug POST protocol
    json_object *errMessage;
    AFB_error status;
    struct MHD_Response *webResponse;
    const char *serialized;
    AFB_request *request;
    AFB_PostHandle *postHandle;
    AFB_PostRequest postRequest;
    int ret;
  
    // if post data may come in multiple calls
    if (0 == strcmp(method, MHD_HTTP_METHOD_POST)) {
        const char *encoding, *param;
        int contentlen = -1;
        postHandle = *con_cls;

        // This is the initial post event let's create form post structure POST datas come in multiple events
        if (postHandle == NULL) {

            // allocate application POST processor handle to zero
            postHandle = calloc(1, sizeof (AFB_PostHandle));
            postHandle->uid = postcount++; // build a UID for DEBUG
            
            // Let make sure we have the right encoding and a valid length
            encoding = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_CONTENT_TYPE);
            
            // We are facing an empty post let's process it as a get
            if (encoding == NULL) {
                request= createRequest (connection, session, url);
                goto ProcessApiCall;
            }
        
            // Form post is handle through a PostProcessor and call API once per form key
            if (strcasestr(encoding, FORM_CONTENT) != NULL) {
                if (verbose) fprintf(stderr, "Create PostForm[uid=%d]\n", postHandle->uid);

                request = createRequest (connection, session, url);
                if (request->jresp != NULL) {
                    errMessage = request->jresp;
                    goto ExitOnError;
                }
                postHandle = malloc(sizeof (AFB_PostHandle)); // allocate application POST processor handle
                postHandle->type   = AFB_POST_FORM;
                postHandle->pp     = MHD_create_post_processor (connection, MAX_POST_SIZE, doPostIterate, postHandle);
                postHandle->private= (void*)request;
                *con_cls = postHandle;  // update context with posthandle
                
                if (NULL == postHandle->pp) {
                    fprintf(stderr,"OOPS: Internal error fail to allocate MHD_create_post_processor\n");
                    free (postHandle);
                    return MHD_NO;
                }
                return MHD_YES;
            }           
        
            // POST json is store into a buffer and present in one piece to API
            if (strcasestr(encoding, JSON_CONTENT) != NULL) {

                param = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_CONTENT_LENGTH);
                if (param) sscanf(param, "%i", &contentlen);

                // Because PostJson are build in RAM size is constrained
                if (contentlen > MAX_POST_SIZE) {
                    errMessage = jsonNewMessage(AFB_FATAL, "Post Date to big %d > %d", contentlen, MAX_POST_SIZE);
                    goto ExitOnError;
                }

                // Size is OK, let's allocate a buffer to hold post data
                postHandle->type = AFB_POST_JSON;
                postHandle->private = malloc(contentlen + 1); // allocate memory for full POST data + 1 for '\0' enf of string

                // if (verbose) fprintf(stderr, "Create PostJson[uid=%d] Size=%d\n", postHandle->uid, contentlen);
                return MHD_YES;

            } else {
                // We only support Json and Form Post format
                errMessage = jsonNewMessage(AFB_FATAL, "Post Date wrong type encoding=%s != %s", encoding, JSON_CONTENT);
                goto ExitOnError;                
            }   
        }

        // This time we receive partial/all Post data. Note that even if we get all POST data. We should nevertheless
        // return MHD_YES and not process the request directly. Otherwise Libmicrohttpd is unhappy and fails with
        // 'Internal application error, closing connection'.            
        if (*upload_data_size) {
    
            if (postHandle->type == AFB_POST_FORM) {
                // if (verbose) fprintf(stderr, "Processing PostForm[uid=%d]\n", postHandle->uid);
                MHD_post_process (postHandle->pp, upload_data, *upload_data_size);
            }
            
            // Process JsonPost request when buffer is completed let's call API    
            if (postHandle->type == AFB_POST_JSON) {
                // if (verbose) fprintf(stderr, "Updating PostJson[uid=%d]\n", postHandle->uid);
                memcpy(&postHandle->private[postHandle->len], upload_data, *upload_data_size);
                postHandle->len = postHandle->len + *upload_data_size;
            }
            
            *upload_data_size = 0;
            return MHD_YES;
            
        } else {  // we have finish with Post reception let's finish the work
            
            // Create a request structure to finalise the request
            request= createRequest (connection, session, url);
            if (request->jresp != NULL) {
                errMessage = request->jresp;
                goto ExitOnError;
            }
            
            // Postform add application context handle to request
            if (postHandle->type == AFB_POST_FORM) {
               postRequest.data = (char*) postHandle;
               postRequest.type = postHandle->type;
               request->post = &postRequest;
            }
            
            if (postHandle->type == AFB_POST_JSON) {
                // if (verbose) fprintf(stderr, "Processing PostJson[uid=%d]\n", postHandle->uid);

                param = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_CONTENT_LENGTH);
                if (param) sscanf(param, "%i", &contentlen);

                // At this level we're may verify that we got everything and process DATA
                if (postHandle->len != contentlen) {
                    errMessage = jsonNewMessage(AFB_FATAL, "Post Data Incomplete UID=%d Len %d != %d", postHandle->uid, contentlen, postHandle->len);
                    goto ExitOnError;
                }

                // Before processing data, make sure buffer string is properly ended
                postHandle->private[postHandle->len] = '\0';
                postRequest.data = postHandle->private;
                postRequest.type = postHandle->type;
                request->post = &postRequest;

                // if (verbose) fprintf(stderr, "Close Post[%d] Buffer=%s\n", postHandle->uid, request->post->data);
            }
        }
    } else {
        // this is a get we only need a request
        request= createRequest (connection, session, url);
    };

ProcessApiCall:    
    // Request is ready let's call API without any extra handle
    status = findAndCallApi (request, NULL);

    serialized = json_object_to_json_string(request->jresp);
    webResponse = MHD_create_response_from_buffer(strlen(serialized), (void*) serialized, MHD_RESPMEM_MUST_COPY);
    
    // client did not pass token on URI let's use cookies 
    if ((!request->restfull) && (request->client != NULL)) {
       char cookie[64]; 
       snprintf (cookie, sizeof (cookie), "%s=%s", COOKIE_NAME,  request->client->uuid); 
       MHD_add_response_header (webResponse, MHD_HTTP_HEADER_SET_COOKIE, cookie);
    }
    
    // if requested add an error status
    if (request->errcode != 0)  ret=MHD_queue_response (connection, request->errcode, webResponse);
    else MHD_queue_response(connection, MHD_HTTP_OK, webResponse);
    
    MHD_destroy_response(webResponse);
    json_object_put(request->jresp); // decrease reference rqtcount to free the json object
    freeRequest (request);
    return MHD_YES;

ExitOnError:
    freeRequest (request);
    serialized = json_object_to_json_string(errMessage);
    webResponse = MHD_create_response_from_buffer(strlen(serialized), (void*) serialized, MHD_RESPMEM_MUST_COPY);
    MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, webResponse);
    MHD_destroy_response(webResponse);
    json_object_put(errMessage); // decrease reference rqtcount to free the json object
    return MHD_YES;
}


// Loop on plugins. Check that they have the right type, prepare a JSON object with prefix
STATIC AFB_plugin ** RegisterJsonPlugins(AFB_plugin **plugins) {
    int idx, jdx;

    for (idx = 0; plugins[idx] != NULL; idx++) {
        if (plugins[idx]->type != AFB_PLUGIN_JSON) {
            fprintf(stderr, "ERROR: AFSV plugin[%d] invalid type=%d != %d\n", idx, AFB_PLUGIN_JSON, plugins[idx]->type);
        } else {
            // some sanity controls
            if ((plugins[idx]->prefix == NULL) || (plugins[idx]->info == NULL) || (plugins[idx]->apis == NULL)) {
                if (plugins[idx]->prefix == NULL) plugins[idx]->prefix = "No URL prefix for APIs";
                if (plugins[idx]->info == NULL) plugins[idx]->info = "No Info describing plugin APIs";
                fprintf(stderr, "ERROR: plugin[%d] invalid prefix=%s info=%s", idx, plugins[idx]->prefix, plugins[idx]->info);
                return NULL;
            }

            if (verbose) fprintf(stderr, "Loading plugin[%d] prefix=[%s] info=%s\n", idx, plugins[idx]->prefix, plugins[idx]->info);
            
            // Prebuild plugin jtype to boost API response
            plugins[idx]->jtype = json_object_new_string(plugins[idx]->prefix);
            json_object_get(plugins[idx]->jtype); // increase reference count to make it permanent
            plugins[idx]->prefixlen = strlen(plugins[idx]->prefix);
            
              
            // Prebuild each API jtype to boost API json response
            for (jdx = 0; plugins[idx]->apis[jdx].name != NULL; jdx++) {
                AFB_privateApi *private = malloc (sizeof (AFB_privateApi));
                if (plugins[idx]->apis[jdx].private != NULL) {
                    fprintf (stderr, "WARNING: plugin=%s api=%s private handle should be NULL=0x%x\n"
                            ,plugins[idx]->prefix,plugins[idx]->apis[jdx].name, plugins[idx]->apis[jdx].private);
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

    plugins[i++] = tokenRegister(session);
    plugins[i++] = helloWorldRegister(session);
    plugins[i++] = samplePostRegister(session);
#ifdef HAVE_AUDIO_PLUGIN
    plugins[i++] = audioRegister(session);
#endif
#ifdef HAVE_RADIO_PLUGIN
    plugins[i++] = radioRegister(session),
#endif
    plugins[i++] = NULL;
    
    // complete plugins and save them within current sessions    
    session->plugins = RegisterJsonPlugins(plugins);
}
