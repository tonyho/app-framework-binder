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
  struct {
         char  *url;
         char  *path;
       } aliasdir[MAX_ALIAS];  // alias mapping for icons,apps,...
};

#endif /* LOCAL_DEF_H */
