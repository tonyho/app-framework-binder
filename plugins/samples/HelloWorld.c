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

STATIC json_object* pingSample (AFB_request *request) {
    static pingcount = 0;
    json_object *response;
    char query [512];
    int len;

    // request all query key/value
    len = getQueryAll (request, query, sizeof(query));
    if (len == 0) strcpy (query,"NoSearchQueryList");
    
    // check if we have some post data
    if (request->post == NULL)  request->post->data="NoData";  
        
    // return response to caller
    response = jsonNewMessage(AFB_SUCCESS, "Ping Binder Daemon %d query={%s} PostData: \'%s\' ", pingcount++, query, request->post);
    
    if (verbose) fprintf(stderr, "%d: \n", pingcount);
    return (response);
}

STATIC json_object* pingFail (AFB_request *request) {
    return NULL;
}

STATIC json_object* pingBug (AFB_request *request) {
    int a,b,c;
    
    fprintf (stderr, "Use --timeout=10 to trap error\n");
    b=4;
    c=0;
    a=b/c;
    
    // should never return
    return NULL;
}


// For samples https://linuxprograms.wordpress.com/2010/05/20/json-c-libjson-tutorial/
STATIC json_object* pingJson (AFB_session *session, AFB_request *request) {
    json_object *jresp, *embed;    
    
    jresp = json_object_new_object();
    json_object_object_add(jresp, "myString", json_object_new_string ("Some String"));
    json_object_object_add(jresp, "myInt", json_object_new_int (1234));
     
    embed  = json_object_new_object();
    json_object_object_add(embed, "subObjString", json_object_new_string ("Some String"));
    json_object_object_add(embed, "subObjInt", json_object_new_int (5678));
    
    json_object_object_add(jresp,"eobj", embed);
    
    return jresp;
}

// NOTE: this sample does not use session to keep test a basic as possible
//       in real application most APIs should be protected with AFB_SESSION_CHECK
STATIC  AFB_restapi pluginApis[]= {
  {"ping"     , AFB_SESSION_NONE, (AFB_apiCB)pingSample  , "Ping Application Framework"},
  {"pingnull" , AFB_SESSION_NONE, (AFB_apiCB)pingFail    , "Return NULL"},
  {"pingbug"  , AFB_SESSION_NONE, (AFB_apiCB)pingBug     , "Do a Memory Violation"},
  {"pingJson" , AFB_SESSION_NONE, (AFB_apiCB)pingJson    , "Return a JSON object"},
  {NULL}
};


PUBLIC AFB_plugin *pluginRegister () {
    AFB_plugin *plugin = malloc (sizeof (AFB_plugin));
    plugin->type  = AFB_PLUGIN_JSON;
    plugin->info  = "Application Framework Binder Service";
    plugin->prefix= "dbus";        
    plugin->apis  = pluginApis;
    return (plugin);
};
