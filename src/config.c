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

PUBLIC int verbose;
STATIC AFB_errorT   AFBerr [AFB_SUCCESS+1];
STATIC json_object *jTypeStatic;

/* ------------------------------------------------------------------------------
 * Get localtime and return in a string
 * ------------------------------------------------------------------------------ */

PUBLIC char * configTime (void) {
  static char reqTime [26];
  time_t tt;
  struct tm *rt;

  /* Get actual Date and Time */
  time (&tt);
  rt = localtime (&tt);

  strftime (reqTime, sizeof (reqTime), "(%d-%b %H:%M)",rt);

  // return pointer on static data
  return (reqTime);
}

// load config from disk and merge with CLI option
PUBLIC AFB_error configLoadFile (AFB_session * session, AFB_config *cliconfig) {
   static char cacheTimeout [10];
   int fd;
   json_object * AFBConfig, *value;
   
   // default HTTP port
   if (cliconfig->httpdPort == 0) session->config->httpdPort=1234;
   else session->config->httpdPort=cliconfig->httpdPort;
   
   // default Plugin API timeout
   if (cliconfig->apiTimeout == 0) session->config->apiTimeout=10;
   else session->config->apiTimeout=cliconfig->apiTimeout;

   // cache timeout default one hour
   if (cliconfig->cacheTimeout == 0) session->config->cacheTimeout=3600;
   else session->config->cacheTimeout=cliconfig->cacheTimeout;

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

   if  (cliconfig->smack == NULL) {
       session->config->smack = "demo";
   } else {
       session->config->smack= cliconfig->smack;
   }

   if  (cliconfig->smack == NULL) {
       session->config->plugins = "all";
   } else {
       session->config->plugins= cliconfig->plugins;
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
   
   if (!cliconfig->smack && json_object_object_get_ex (AFBConfig, "smack", &value)) {
      session->config->smack =  strdup (json_object_get_string (value));
   }
   
   if (!cliconfig->plugins && json_object_object_get_ex (AFBConfig, "plugins", &value)) {
      session->config->plugins =  strdup (json_object_get_string (value));
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
   
   if (!cliconfig->setuid && json_object_object_get_ex (AFBConfig, "setuid", &value)) {
      session->config->setuid = json_object_get_int (value);
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
   
   // cacheTimeout is an integer but HTTPd wants it as a string
   snprintf (cacheTimeout, sizeof (cacheTimeout),"%d", session->config->cacheTimeout);
   session->cacheTimeout = cacheTimeout; // httpd uses cacheTimeout string version
   
   json_object_put   (AFBConfig);    // decrease reference count to free the json object

 
   
   return AFB_SUCCESS;
}

// Save the config on disk
PUBLIC void configStoreFile (AFB_session * session) {
   json_object * AFBConfig;
   time_t rawtime;
   struct tm * timeinfo;
   int err;

   AFBConfig =  json_object_new_object();

   // add a timestamp and store session on disk
   time ( &rawtime );  timeinfo = localtime ( &rawtime );
   // A copy of the string is made and the memory is managed by the json_object
   json_object_object_add (AFBConfig, "jtype"         , json_object_new_string (AFB_CONFIG_JTYPE));
   json_object_object_add (AFBConfig, "timestamp"     , json_object_new_string (asctime (timeinfo)));
   json_object_object_add (AFBConfig, "rootdir"       , json_object_new_string (session->config->rootdir));
   json_object_object_add (AFBConfig, "rootapi"       , json_object_new_string (session->config->rootapi));
   json_object_object_add (AFBConfig, "rootbase"      , json_object_new_string (session->config->rootbase));
   json_object_object_add (AFBConfig, "smack"         , json_object_new_string (session->config->smack));
   json_object_object_add (AFBConfig, "plugins"       , json_object_new_string (session->config->plugins));
   json_object_object_add (AFBConfig, "sessiondir"    , json_object_new_string (session->config->sessiondir));
   json_object_object_add (AFBConfig, "pidfile"       , json_object_new_string (session->config->pidfile));
   json_object_object_add (AFBConfig, "httpdPort"     , json_object_new_int (session->config->httpdPort));
   json_object_object_add (AFBConfig, "setuid"        , json_object_new_int (session->config->setuid));
   json_object_object_add (AFBConfig, "localhostonly" , json_object_new_int (session->config->localhostOnly));
   json_object_object_add (AFBConfig, "cachetimeout"  , json_object_new_int (session->config->cacheTimeout));
   json_object_object_add (AFBConfig, "apitimeout"    , json_object_new_int (session->config->apiTimeout));

   err = json_object_to_file (session->config->configfile, AFBConfig);
   json_object_put   (AFBConfig);    // decrease reference count to free the json object
   if (err < 0) {
      fprintf(stderr, "AFB: Fail to save config on disk [%s]\n ", session->config->configfile);
   }
}


PUBLIC AFB_session *configInit () {

  AFB_session *session;
  AFB_config  *config;
  int idx, verbosesav;


  session = malloc (sizeof (AFB_session));
  memset (session,0, sizeof (AFB_session));

  // create config handle
  config = malloc (sizeof (AFB_config));
  memset (config,0, sizeof (AFB_config));

  // stack config handle into session
  session->config = config;

  jTypeStatic = json_object_new_string ("AFB_message");

  // initialise JSON constant messages and increase reference count to make them permanent
  verbosesav = verbose;
  verbose = 0;  // run initialisation in silent mode


  for (idx = 0; idx <= AFB_SUCCESS; idx++) {
     AFBerr[idx].level = idx;
     AFBerr[idx].label = ERROR_LABEL [idx];
     AFBerr[idx].json  = jsonNewMessage (idx, NULL);
  }
  verbose = verbosesav;
  
  // Load Plugins
  initPlugins (session);
  
  return (session);
}


// get JSON object from error level and increase its reference count
PUBLIC json_object *jsonNewStatus (AFB_error level) {

  json_object *target =  AFBerr[level].json;
  json_object_get (target);

  return (target);
}

// get AFB object type with adequate usage count
PUBLIC json_object *jsonNewjtype (void) {
  json_object_get (jTypeStatic); // increase reference count
  return (jTypeStatic);
}

// build an ERROR message and return it as a valid json object
PUBLIC  json_object *jsonNewMessage (AFB_error level, char* format, ...) {
   static int count = 0;
   json_object * AFBResponse;
   va_list args;
   char message [512];

   // format message
   if (format != NULL) {
       va_start(args, format);
       vsnprintf (message, sizeof (message), format, args);
       va_end(args);
   }

   AFBResponse = json_object_new_object();
   json_object_object_add (AFBResponse, "jtype", jsonNewjtype ());
   json_object_object_add (AFBResponse, "status" , json_object_new_string (ERROR_LABEL[level]));
   if (format != NULL) {
        json_object_object_add (AFBResponse, "info"   , json_object_new_string (message));
   }
   if (verbose) {
        fprintf (stderr, "AFB:%-6s [%3d]: ", AFBerr [level].label, count++);
        if (format != NULL) {
            fprintf (stderr, "%s", message);
        } else {
            fprintf (stderr, "No Message");
        }
        fprintf (stderr, "\n");
   }

   return (AFBResponse);
}

// Dump a message on stderr
PUBLIC void jsonDumpObject (json_object * jObject) {

   if (verbose) {
        fprintf (stderr, "AFB:dump [%s]\n", json_object_to_json_string(jObject));
   }
}

