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

STATIC json_object* pingAfbs (AFB_plugin *plugin, AFB_session *session, struct MHD_Connection *connection, AFB_request *request) {
    static pingcount=0;
    json_object *response;
    response = jsonNewMessage(AFB_SUCCESS, "Ping Application Framework %d", pingcount++);
    if (verbose) fprintf(stderr, "%d: \n", pingcount);
    return (response);
};


STATIC  AFB_restapi pluginApis[]= {
  {"/ping"     , (AFB_apiCB)pingSample ,"Ping Service"},
  {"/get-all"  , (AFB_apiCB)pingAfbs ,"Ping Application Framework"},
  {"/get-one"  , (AFB_apiCB)pingSample ,"Verbose Mode"},
  {"/start-one", (AFB_apiCB)pingSample ,"Verbose Mode"},
  {"/stop-one" , (AFB_apiCB)pingSample ,"Verbose Mode"},
  {"/probe-one", (AFB_apiCB)pingSample ,"Verbose Mode"},
  {"/ctx-store", (AFB_apiCB)pingSample ,"Verbose Mode"},
  {"/ctx-load" , (AFB_apiCB)pingSample ,"Verbose Mode"},
  {0,0,0}
};

PUBLIC AFB_plugin *dbusRegister (AFB_session *session) {
    AFB_plugin *plugin = malloc (sizeof (AFB_plugin));
    
    plugin->info  = "Application Framework Binder Service";
    plugin->prefix= "dbus";        
    plugin->apis  = pluginApis;
    
    return (plugin);
};