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

#define _GNU_SOURCE

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

#define AJQ_VERSION "0.1"

/* other definitions --------------------------------------------------- */

// Note: because of a bug in libmagic MAGIC_DB NULL should not be used for default
#define MAGIC_DB "/usr/share/misc/magic.mgc"
#define OPA_INDEX "index.html"

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

// prebuild json error are constructed in config.c
typedef enum  { AFB_FALSE, AFB_TRUE, AFB_FATAL, AFB_FAIL, AFB_WARNING, AFB_EMPTY, AFB_SUCCESS} AFB_error;

extern char *ERROR_LABEL[];
#define ERROR_LABEL_DEF {"false", "true","fatal", "fail", "warning", "empty", "success"}

#define BANNER "<html><head><title>Application Framework Binder</title></head><body>Application Framework </body></html>"
#define JSON_CONTENT  "application/json"
#define MAX_POST_SIZE  4096   // maximum size for POST data

// use to check anonymous data when using dynamic loadable lib
typedef enum  {AFB_PLUGIN=1234, AFB_REQUEST=5678} AFB_type;

// Error code are requested through function to manage json usage count
typedef struct {
  int   level;
  char* label;
  json_object *json;
} AFB_errorT;

// Post handler
typedef struct {
  char* data;
  int   len;
  int   uid;
} AFB_HttpPost;

typedef struct {
  char  path[512];
  int   fd;
} AFB_staticfile;


// some usefull static object initialized when entering listen loop.
extern int verbose;
// MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "value");
typedef struct {
  const char *url;
  char *plugin;
  char *api;
  char *post;
  struct MHD_Connection *connection;
} AFB_request;

typedef struct {
     char    *msg;
     size_t  len;
} AFB_redirect_msg;

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
  uid_t setuid;
  int  cacheTimeout;
  int  apiTimeout;
} AFB_config;

// Command line structure hold cli --command + help text
typedef struct {
  int  val;        // command number within application
  int  has_arg;    // command number within application
  char *name;      // command as used in --xxxx cli
  char *help;      // help text
} AFB_options;

typedef json_object* (*AFB_apiCB)();

// API definition
typedef struct {
  char *name;
  AFB_apiCB callback;
  char *info;
  void * handle;
} AFB_restapi;

// Plugin definition
typedef struct {
  AFB_type type;  
  char *info;
  char *prefix;
  size_t prefixlen;
  json_object *jtype;
  AFB_restapi *apis;
} AFB_plugin;

typedef struct {
  AFB_config  *config;   // pointer to current config
  // List of commands to execute
  int  killPrevious;
  int  background;        // run in backround mode
  int  foreground;        // run in forground mode
  int  checkAlsa;         // Display active Alsa Board
  int  configsave;        // Save config on disk on start
  char *cacheTimeout;     // http require timeout to be a string
  void *httpd;            // anonymous structure for httpd handler
  int  fakemod;           // respond to GET/POST request without interacting with sndboard
  int  forceexit;         // when autoconfig from script force exit before starting server
  AFB_plugin **plugins;   // pointer to REST/API plugins 
  magic_t  magic;         // Mime type file magic lib
} AFB_session;


#include "proto-def.h"
