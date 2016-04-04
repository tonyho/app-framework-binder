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

typedef struct queryHandleT {
     char    *msg;
     size_t  idx;
     size_t  len;
} queryHandleT;

static int getQueryCB (queryHandleT *query, struct afb_arg arg) {
    if (query->idx >= query->len)
	return 0;
    query->idx += (unsigned)snprintf (&query->msg[query->idx], query->len-query->idx, " %s: %s\'%s\',", arg.name, arg.is_file?"FILE=":"", arg.value);
    return 1; /* continue to iterate */
}

// Helper to retrieve argument from  connection
static size_t getQueryAll(struct afb_req request, char *buffer, size_t len) {
    queryHandleT query;
    buffer[0] = '\0'; // start with an empty string
    query.msg = buffer;
    query.len = len;
    query.idx = 0;

    afb_req_iterate(request, (void*)getQueryCB, &query);
    buffer[len-1] = 0;
    return query.idx >= len ? len - 1 : query.idx;
}

static void ping (struct afb_req request, json_object *jresp)
{
    static int pingcount = 0;
    char query [512];
    size_t len;

    // request all query key/value
    len = getQueryAll (request, query, sizeof(query));
    if (len == 0) strcpy (query,"NoSearchQueryList");
    
    // return response to caller
//    response = jsonNewMessage(AFB_SUCCESS, "Ping Binder Daemon %d query={%s}", pingcount++, query);
    afb_req_success_f(request, jresp, "Ping Binder Daemon %d query={%s}", pingcount++, query);
    
    fprintf(stderr, "%d: \n", pingcount);
}

static void pingSample (struct afb_req request)
{
	ping(request, json_object_new_string ("Some String"));
}

static void pingFail (struct afb_req request)
{
	afb_req_fail(request, "failed", "Ping Binder Daemon fails");
}

static void pingNull (struct afb_req request)
{
	ping(request, NULL);
}

static void pingBug (struct afb_req request)
{
    int a,b,c;
    
    b=4;
    c=0;
    a=b/c;
    
}


// For samples https://linuxprograms.wordpress.com/2010/05/20/json-c-libjson-tutorial/
static void pingJson (struct afb_req request) {
    json_object *jresp, *embed;    
    
    jresp = json_object_new_object();
    json_object_object_add(jresp, "myString", json_object_new_string ("Some String"));
    json_object_object_add(jresp, "myInt", json_object_new_int (1234));
     
    embed  = json_object_new_object();
    json_object_object_add(embed, "subObjString", json_object_new_string ("Some String"));
    json_object_object_add(embed, "subObjInt", json_object_new_int (5678));
    
    json_object_object_add(jresp,"eobj", embed);

    ping(request, jresp);
}

// NOTE: this sample does not use session to keep test a basic as possible
//       in real application most APIs should be protected with AFB_SESSION_CHECK
static const struct AFB_restapi pluginApis[]= {
  {"ping"     , AFB_SESSION_NONE, pingSample  , "Ping Application Framework"},
  {"pingfail" , AFB_SESSION_NONE, pingFail    , "Fails"},
  {"pingnull" , AFB_SESSION_NONE, pingNull    , "Return NULL"},
  {"pingbug"  , AFB_SESSION_NONE, pingBug     , "Do a Memory Violation"},
  {"pingJson" , AFB_SESSION_NONE, pingJson    , "Return a JSON object"},
  {NULL}
};

static const struct AFB_plugin plugin_desc = {
	.type = AFB_PLUGIN_JSON,
	.info = "Minimal Hello World Sample",
	.prefix = "hello",
	.apis = pluginApis
};

const struct AFB_plugin *pluginRegister (const struct AFB_interface *itf)
{
	return &plugin_desc;
}
