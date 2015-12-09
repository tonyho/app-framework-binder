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


#include "local-def.h"

STATIC json_object* wrongApi (AFB_session *session, AFB_request *request, void* handle) {
    int zero=0;
    int bug=1234;
    int impossible;
    
    impossible=bug/zero;
}

STATIC json_object* pingSample (AFB_session *session, AFB_request *request, void* handle) {
    static pingcount = 0;
    json_object *response;
    char query [512];

    // request all query key/value
    getQueryAll (request, query, sizeof(query)); 
    
    // check if we have some post data
    if (request->post == NULL)  request->post="NoData";  
        
    // return response to caller
    response = jsonNewMessage(AFB_SUCCESS, "Ping Binder Daemon %d query={%s} PostData: \'%s\' ", pingcount++, query, request->post);
    
    if (verbose) fprintf(stderr, "%d: \n", pingcount);
    return (response);
}


STATIC struct {
    void * somedata;
} handle;


STATIC  AFB_restapi pluginApis[]= {
  {"ping"     , (AFB_apiCB)pingSample , "Ping Application Framework", NULL},
  {"error"    , (AFB_apiCB)wrongApi   , "Ping Application Framework", NULL},
  {"ctx-store", (AFB_apiCB)pingSample , "Verbose Mode", NULL},
  {"ctx-load" , (AFB_apiCB)pingSample , "Verbose Mode", NULL},
  {0,0,0}
};

PUBLIC AFB_plugin *alsaRegister (AFB_session *session) {
    AFB_plugin *plugin = malloc (sizeof (AFB_plugin));
    plugin->type  = AFB_PLUGIN;
    plugin->info  = "Application Framework Binder Service";
    plugin->prefix  = "alsa";        
    plugin->apis  = pluginApis;
    
    return (plugin);
};