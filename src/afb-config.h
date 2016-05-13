/*
 * Copyright (C) 2015, 2016 "IoT.bzh"
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef LOCAL_DEF_H
#define LOCAL_DEF_H

#pragma once

/* other definitions --------------------------------------------------- */

// Note: because of a bug in libmagic MAGIC_DB NULL should not be used for default
#define MAX_ALIAS 10           // max number of aliases

#define DEFLT_CNTX_TIMEOUT  3600   // default Client Connection Timeout
#define DEFLT_API_TIMEOUT   0      // default Plugin API Timeout [0=NoLimit for Debug Only]
#define DEFLT_CACHE_TIMEOUT 100000 // default Static File Chache [Client Side Cache 100000~=1day]
#define DEFLT_AUTH_TOKEN    NULL   // expect for debug should == NULL
#define DEFLT_HTTP_TIMEOUT  15     // Max MibMicroHttp timeout

#define CTX_NBCLIENTS   10   // allow a default of 10 authenticated clients

struct afb_config_item
{
	struct afb_config_item *previous;
	int kind;
	char *value;
};

// main config structure
struct afb_config
{
  char *console;           // console device name (can be a file or a tty)
  int   httpdPort;
  char *ldpaths;           // list of plugins directories
  char *rootdir;           // base dir for httpd file download
  char *rootbase;          // Angular HTML5 base URL
  char *rootapi;           // Base URL for REST APIs
  char *sessiondir;        // where to store mixer session files
  char *token;             // initial authentication token [default NULL no session]
  int  background;        // run in backround mode
  int  readyfd;           // a #fd to signal when ready to serve
  int  cacheTimeout;
  int  apiTimeout;
  int  cntxTimeout;        // Client Session Context timeout
  int mode;           // mode of listening
  int aliascount;
  struct afb_config_item *items;
  struct {
         char  *url;
         char  *path;
       } aliasdir[MAX_ALIAS];  // alias mapping for icons,apps,...
};

#endif /* LOCAL_DEF_H */
