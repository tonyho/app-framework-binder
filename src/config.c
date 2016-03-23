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

   References:
     https://www.gnu.org/software/libmicrohttpd/manual/html_node/index.html#Top
     http://www-01.ibm.com/support/knowledgecenter/SSB23S_1.1.0.9/com.ibm.ztpf-ztpfdf.doc_put.09/gtpc2/cpp_vsprintf.html?cp=SSB23S_1.1.0.9%2F0-3-8-1-0-16-8

*/

#include "../include/local-def.h"
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>

#define AFB_CONFIG_JTYPE "AFB_config"

PUBLIC  char *ERROR_LABEL[]=ERROR_LABEL_DEF;

// load config from disk and merge with CLI option
PUBLIC AFB_error configLoadFile (AFB_session * session, AFB_config *cliconfig) {
   static char cacheTimeout [10];
   int fd;
   json_object * AFBConfig, *value;
   
   // TBD integrate alias-dir array with config-file
   session->config->aliasdir = cliconfig->aliasdir;
   session->config->mode = cliconfig->mode;

   // default HTTP port
   if (cliconfig->httpdPort == 0) session->config->httpdPort=1234;
   else session->config->httpdPort=cliconfig->httpdPort;
   
   // default Plugin API timeout
   if (cliconfig->apiTimeout == 0) session->config->apiTimeout=DEFLT_API_TIMEOUT;
   else session->config->apiTimeout=cliconfig->apiTimeout;
   
   // default AUTH_TOKEN
   if (cliconfig->token == NULL) session->config->token= DEFLT_AUTH_TOKEN;
   else session->config->token=cliconfig->token;

   // cache timeout default one hour
   if (cliconfig->cacheTimeout == 0) session->config->cacheTimeout=DEFLT_CACHE_TIMEOUT;
   else session->config->cacheTimeout=cliconfig->cacheTimeout;

   // cache timeout default one hour
   if (cliconfig->cntxTimeout == 0) session->config->cntxTimeout=DEFLT_CNTX_TIMEOUT;
   else session->config->cntxTimeout=cliconfig->cntxTimeout;

   if (cliconfig->rootdir == NULL) {
       session->config->rootdir = getenv("AFBDIR");
       if (session->config->rootdir == NULL) {
           session->config->rootdir = malloc (512);
           strncpy  (session->config->rootdir, getenv("HOME"),512);
           strncat (session->config->rootdir, "/.AFB",512);
       }
       // if directory does not exist createit
       mkdir (session->config->rootdir,  O_RDWR | S_IRWXU | S_IRGRP);
   } else {
       session->config->rootdir =  cliconfig->rootdir;
   }
   
   // if no Angular/HTML5 rootbase let's try '/' as default
   if  (cliconfig->rootbase == NULL) {
       session->config->rootbase = "/opa";
   } else {
       session->config->rootbase= cliconfig->rootbase;
   }
   
   if  (cliconfig->rootapi == NULL) {
       session->config->rootapi = "/api";
   } else {
       session->config->rootapi= cliconfig->rootapi;
   }

   if  (cliconfig->ldpaths == NULL) {
       session->config->ldpaths = PLUGIN_INSTALL_DIR;
   } else {
       session->config->ldpaths= cliconfig->ldpaths;
   }

   // if no session dir create a default path from rootdir
   if  (cliconfig->sessiondir == NULL) {
       session->config->sessiondir = malloc (512);
       strncpy (session->config->sessiondir, session->config->rootdir, 512);
       strncat (session->config->sessiondir, "/sessions",512);
   } else {
       session->config->sessiondir = cliconfig->sessiondir;
   }

   // if no config dir create a default path from sessiondir
   if  (cliconfig->configfile == NULL) {
       session->config->configfile = malloc (512);
       strncpy (session->config->configfile, session->config->sessiondir, 512);
       strncat (session->config->configfile, "/AFB-config.json",512);
   } else {
       session->config->configfile = cliconfig->configfile;
   }

   // if no config dir create a default path from sessiondir
   if  (cliconfig->pidfile == NULL) {
       session->config->pidfile = malloc (512);
       strncpy (session->config->pidfile, session->config->sessiondir, 512);
       strncat (session->config->pidfile, "/AFB-process.pid",512);
   } else {
       session->config->pidfile= cliconfig->pidfile;
   }

   // if no config dir create a default path from sessiondir
   if  (cliconfig->console == NULL) {
       session->config->console = malloc (512);
       strncpy (session->config->console, session->config->sessiondir, 512);
       strncat (session->config->console, "/AFB-console.out",512);
   } else {
       session->config->console= cliconfig->console;
   }
   
   // just upload json object and return without any further processing
   if((fd = open(session->config->configfile, O_RDONLY)) < 0) {
      if (verbose) fprintf (stderr, "AFB:notice: config at %s: %s\n", session->config->configfile, strerror(errno));
      return AFB_EMPTY;
   }

   // openjson from FD is not public API we need to reopen it !!!
   close(fd);
   AFBConfig = json_object_from_file (session->config->configfile);

   // check it is an AFB_config
   if (json_object_object_get_ex (AFBConfig, "jtype", &value)) {
      if (strcmp (AFB_CONFIG_JTYPE, json_object_get_string (value))) {
         fprintf (stderr,"AFB: Error file [%s] is not a valid [%s] type\n ", session->config->configfile, AFB_CONFIG_JTYPE);
         return AFB_FAIL;
      }
   }

   if (!cliconfig->rootdir && json_object_object_get_ex (AFBConfig, "rootdir", &value)) {
      session->config->rootdir =  strdup (json_object_get_string (value));
   }
   
   if (!cliconfig->rootbase && json_object_object_get_ex (AFBConfig, "rootbase", &value)) {
      session->config->rootbase =  strdup (json_object_get_string (value));
   }
   
   if (!cliconfig->rootapi && json_object_object_get_ex (AFBConfig, "rootapi", &value)) {
      session->config->rootapi =  strdup (json_object_get_string (value));
   }
   
   if (!cliconfig->ldpaths && json_object_object_get_ex (AFBConfig, "plugins", &value)) {
      session->config->ldpaths =  strdup (json_object_get_string (value));
   }

   if (!cliconfig->setuid && json_object_object_get_ex (AFBConfig, "setuid", &value)) {
      session->config->setuid = strdup (json_object_get_string (value));
   }
   
   if (!cliconfig->sessiondir && json_object_object_get_ex (AFBConfig, "sessiondir", &value)) {
      session->config->sessiondir = strdup (json_object_get_string (value));
   }
   
   if (!cliconfig->pidfile && json_object_object_get_ex (AFBConfig, "pidfile", &value)) {
      session->config->pidfile = strdup (json_object_get_string (value));
   }

   if (!cliconfig->httpdPort && json_object_object_get_ex (AFBConfig, "httpdPort", &value)) {
      session->config->httpdPort = json_object_get_int (value);
   }
   

   if (!cliconfig->localhostOnly && json_object_object_get_ex (AFBConfig, "localhostonly", &value)) {
      session->config->localhostOnly = json_object_get_int (value);
   }
   
   if (!cliconfig->cacheTimeout && json_object_object_get_ex (AFBConfig, "cachetimeout", &value)) {
      session->config->cacheTimeout = json_object_get_int (value);
   }
   
   if (!cliconfig->apiTimeout && json_object_object_get_ex (AFBConfig, "apitimeout", &value)) {
      session->config->apiTimeout = json_object_get_int (value);
   }
   
   if (!cliconfig->cntxTimeout && json_object_object_get_ex (AFBConfig, "cntxtimeout", &value)) {
      session->config->cntxTimeout = json_object_get_int (value);
   }
   
   // cacheTimeout is an integer but HTTPd wants it as a string
   snprintf (cacheTimeout, sizeof (cacheTimeout),"%d", session->config->cacheTimeout);
   session->cacheTimeout = cacheTimeout; // httpd uses cacheTimeout string version
   
   json_object_put   (AFBConfig);    // decrease reference count to free the json object

 
   
   return AFB_SUCCESS;
}


PUBLIC AFB_session *configInit ()
{
  AFB_session *session = calloc (1, sizeof (AFB_session));
  session->config = calloc (1, sizeof (AFB_config));
  return session;
}

