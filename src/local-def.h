/*
   local-def.h -- provide a REST/HTTP interface

   Copyright (C) 2015, Fulup Ar Foll

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/
#ifndef LOCAL_DEF_H
#define LOCAL_DEF_H

#include <json.h>
#include <magic.h>
#include <microhttpd.h>

/* other definitions --------------------------------------------------- */

// Note: because of a bug in libmagic MAGIC_DB NULL should not be used for default
#define OPA_INDEX "index.html"
#define MAX_ALIAS 10           // max number of aliases
#define COOKIE_NAME   "afb-session"

#define DEFLT_CNTX_TIMEOUT  3600   // default Client Connection Timeout
#define DEFLT_API_TIMEOUT   0      // default Plugin API Timeout [0=NoLimit for Debug Only]
#define DEFLT_API_TIMEOUT   0      // default Plugin API Timeout
#define DEFLT_CACHE_TIMEOUT 100000 // default Static File Chache [Client Side Cache 100000~=1day]
#define DEFLT_AUTH_TOKEN    NULL   // expect for debug should == NULL
#define DEFLT_HTTP_TIMEOUT  15     // Max MibMicroHttp timeout
#define AFB_MAX_PLUGINS     20     // Max number of plugins for a given binder

#ifndef FALSE
  #define FALSE 0
#endif
#ifndef TRUE
  #define TRUE 1
#endif

#define PUBLIC
#define STATIC    static
#define FAILED    -1

#define AUDIO_BUFFER "/tmp/buf"

extern int verbose;  // this is the only global variable

// prebuild json error are constructed in helper-api.c
typedef enum  { AFB_FALSE, AFB_TRUE, AFB_FATAL, AFB_FAIL, AFB_WARNING, AFB_EMPTY, AFB_SUCCESS, AFB_DONE, AFB_UNAUTH} AFB_error;

#define MAX_POST_SIZE  4096   // maximum size for POST data
#define CTX_NBCLIENTS   10   // allow a default of 10 authenticated clients










enum AFB_Mode;


typedef struct {
  char  *url;
  char  *path;
  size_t len;
} AFB_aliasdir;

// main config structure
struct AFB_config
{
  char *console;           // console device name (can be a file or a tty)
  int   httpdPort;
  char *ldpaths;           // list of plugins directories
  char *rootdir;           // base dir for httpd file download
  char *rootbase;          // Angular HTML5 base URL
  char *rootapi;           // Base URL for REST APIs
  char *sessiondir;        // where to store mixer session files
  char *token;             // initial authentication token [default NULL no session]
  int  cacheTimeout;
  int  apiTimeout;
  int  cntxTimeout;        // Client Session Context timeout
  int mode;           // mode of listening
  AFB_aliasdir *aliasdir;  // alias mapping for icons,apps,...
};

// MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "value");
typedef struct {
  const char *uuid;
  const char *url;
  const char *prefix;              // plugin convivial name
  const char *method;
/*
  AFB_PostRequest *post;
*/
  struct json_object *jresp;
  void *context;             // Hold Client Context when using session
  int  restfull;             // request is resfull [uuid token provided]
  int  errcode;              // http error code
  struct AFB_config *config;         // plugin may need access to config
  struct afb_req *areq;
} AFB_request;

struct afb_hsrv_handler;
struct MHD_Daemon;

struct AFB_session
{
  struct AFB_config  *config;   // pointer to current config
  // List of commands to execute
  int  background;        // run in backround mode
  int  foreground;        // run in forground mode
  char *cacheTimeout;     // http require timeout to be a string
  struct MHD_Daemon *httpd;            // structure for httpd handler
  int  fakemod;           // respond to GET/POST request without interacting with sndboard
  int  readyfd;           // a #fd to signal when ready to serve
  magic_t  magic;         // Mime type file magic lib
  struct afb_hsrv_handler *handlers;
};


typedef struct AFB_config AFB_config;
typedef struct AFB_session AFB_session;

#include "proto-def.h"

#endif /* LOCAL_DEF_H */
