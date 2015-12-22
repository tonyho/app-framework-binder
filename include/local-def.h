/*
   alsajson-gw -- provide a REST/HTTP interface to ALSA-Mixer

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

#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <time.h>
#include <json.h>
#include <microhttpd.h>
#include <magic.h>
#include <setjmp.h>
#include <signal.h>
#include <uuid/uuid.h>




#define AJQ_VERSION "0.1"

/* other definitions --------------------------------------------------- */

// Note: because of a bug in libmagic MAGIC_DB NULL should not be used for default
#define MAGIC_DB "/usr/share/misc/magic.mgc"
#define OPA_INDEX "index.html"
#define MAX_ALIAS 10           // max number of aliases
#define COOKIE_NAME   "AJB_session"


#define DEFLT_CNTX_TIMEOUT  3600   // default Client Connection Timeout
#define DEFLT_API_TIMEOUT   0      // default Plugin API Timeout [0=NoLimit for Debug Only]
#define DEFLT_API_TIMEOUT   0      // default Plugin API Timeout
#define DEFLT_CACHE_TIMEOUT 100000 // default Static File Chache [Client Side Cache 100000~=1day]
#define DEFLT_AUTH_TOKEN    NULL   // expect for debug should == NULL
#define DEFLT_HTTP_TIMEOUT  15     // Max MibMicroHttp timeout

typedef int BOOL;
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

// Plugin Type
typedef enum  {AFB_PLUGIN_JSON=123456789, AFB_PLUGIN_JSCRIPT=987654321,  AFB_PLUGIN_RAW=987123546} AFB_pluginE;

// prebuild json error are constructed in config.c
typedef enum  { AFB_FALSE, AFB_TRUE, AFB_FATAL, AFB_FAIL, AFB_WARNING, AFB_EMPTY, AFB_SUCCESS, AFB_DONE, AFB_UNAUTH} AFB_error;

extern char *ERROR_LABEL[];
#define ERROR_LABEL_DEF {"false", "true","fatal", "fail", "warning", "empty", "success"}

#define BANNER "<html><head><title>Application Framework Binder</title></head><body>Application Framework </body></html>"
#define JSON_CONTENT  "application/json"
#define FORM_CONTENT "multipart/form-data"
#define MAX_POST_SIZE  4096   // maximum size for POST data
#define CTX_NBCLIENTS   10   // allow a default of 10 authenticated clients


typedef json_object* (*AFB_apiCB)();

// Error code are requested through function to manage json usage count
typedef struct {
  int   level;
  char* label;
  json_object *json;
} AFB_errorT;

typedef enum  {AFB_POST_NONE=0, AFB_POST_JSON, AFB_POST_FORM} AFB_PostType;

// Post Upload File Handle
typedef struct {
   int   fd; 
   char *path; 
   int  errcode;
   json_object* jresp;
} AFB_PostCtx;

typedef  struct {
    int  len;   // post element size
    char *data; // post data in raw format
    AFB_PostType type; // Json type
} AFB_PostRequest;
  
// Post handler
typedef struct {
  void*  ctx;               // Application context
  int    len;               // current len for post
  int    uid;               // post uid for debug
  AFB_PostType type;        // JSON or FORM
  AFB_apiCB  completeCB;    // callback when post is completed
  char   *private;          // use internally to keep track or partial buffer
  struct MHD_PostProcessor *pp; // iterator handle
} AFB_PostHandle;

typedef struct {
    enum MHD_ValueKind kind; // kind type of the value
    const char *key;         // key 0-terminated key for the value
    const char *filename;    // filename of the uploaded file, NULL if not known
    const char *mimetype;    // content_type mime-type of the data, NULL if not known
    const char *encoding;    // transfer_encoding encoding of the data, NULL if not known
    const char *data;        // data pointer to size bytes of data at the specified offset
    uint64_t   offset;       // offset of data in the overall value
    size_t     len;          // number of bytes in data available
} AFB_PostItem;

typedef struct {
  char  path[512];
  int   fd;
} AFB_staticfile;

typedef struct {
  char  *url;
  char  *path;
  size_t len;
} AFB_aliasdir;

// Command line structure hold cli --command + help text
typedef struct {
  int  val;        // command number within application
  int  has_arg;    // command number within application
  char *name;      // command as used in --xxxx cli
  char *help;      // help text
} AFB_options;

// main config structure
typedef struct {
  char *logname;           // logfile path for info & error log
  char *console;           // console device name (can be a file or a tty)
  int  localhostOnly;
  int   httpdPort;
  char *smack;             // smack label
  char *plugins;           // list of requested plugins
  char *rootdir;           // base dir for httpd file download
  char *rootbase;          // Angular HTML5 base URL
  char *rootapi;           // Base URL for REST APIs
  char *pidfile;           // where to store pid when running background
  char *sessiondir;        // where to store mixer session files
  char *configfile;        // where to store configuration on gateway exit
  char *setuid;            // downgrade uid to username
  char *token;             // initial authentication token [default NULL no session]
  int  cacheTimeout;
  int  apiTimeout;
  int  cntxTimeout;        // Client Session Context timeout
  AFB_aliasdir *aliasdir;  // alias mapping for icons,apps,...
} AFB_config;

typedef struct {
  int  len;        // command number within application
  json_object *jtype;
} AFB_privateApi;


typedef struct {
     char    *msg;
     size_t  len;
} AFB_redirect_msg;

// Enum for Session/Token/Authentication middleware
typedef enum  {AFB_SESSION_NONE, AFB_SESSION_CREATE, AFB_SESSION_CLOSE, AFB_SESSION_RENEW, AFB_SESSION_CHECK} AFB_sessionE;

// API definition
typedef struct {
  char *name;
  AFB_sessionE session;
  AFB_apiCB callback;
  char *info;
  AFB_privateApi *private;
} AFB_restapi;

// Plugin definition
typedef struct {
  AFB_pluginE type;  
  char *info;
  char *prefix;
  size_t prefixlen;
  json_object *jtype;
  AFB_restapi *apis;
  void *handle;
  int  ctxCount;
  AFB_apiCB freeCtxCB;  // callback to free application context [null for standard free]
} AFB_plugin;


// User Client Session Context
typedef struct {
  int  cid;             // index 0 if global
  char uuid[37];        // long term authentication of remote client
  char token[37];       // short term authentication of remote client
  time_t timeStamp;     // last time token was refresh
  int   restfull;       // client does not use cookie
  void *ctx;            // application specific context
  AFB_plugin *plugin;   // provide callback and easy access to plugin
} AFB_clientCtx;

// MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "value");
typedef struct {
  const char *url;
  char *plugin;
  char *api;
  AFB_PostRequest *post;
  json_object *jresp;
  AFB_clientCtx *client;      // needed because libmicrohttp cannot create an empty response
  int   restfull;             // request is resfull [uuid token provided]
  int   errcode;              // http error code
  sigjmp_buf checkPluginCall; // context save for timeout set/longjmp
  AFB_config *config;         // plugin may need access to config
  struct MHD_Connection *connection;
  AFB_plugin **plugins;
} AFB_request;


typedef struct {
  AFB_config  *config;   // pointer to current config
  // List of commands to execute
  int  killPrevious;
  int  background;        // run in backround mode
  int  foreground;        // run in forground mode
  int  configsave;        // Save config on disk on start
  char *cacheTimeout;     // http require timeout to be a string
  void *httpd;            // anonymous structure for httpd handler
  int  fakemod;           // respond to GET/POST request without interacting with sndboard
  int  forceexit;         // when autoconfig from script force exit before starting server
  AFB_plugin **plugins;   // pointer to REST/API plugins 
  int pluginCount;        // loaded plugins count
  magic_t  magic;         // Mime type file magic lib
  sigjmp_buf restartCkpt; // context save for restart set/longjmp
} AFB_session;



#include "proto-def.h"

#endif /* LOCAL_DEF_H */
