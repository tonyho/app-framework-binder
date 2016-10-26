/*
 * Copyright (C) 2015, 2016 "IoT.bzh"
 * Author "Fulup Ar Foll"
 * Author José Bollo <jose.bollo@iot.bzh>
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

#define _GNU_SOURCE
#define NO_BINDING_VERBOSE_MACRO

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <getopt.h>

#include <systemd/sd-event.h>

#include "afb-config.h"
#include "afb-hswitch.h"
#include "afb-apis.h"
#include "afb-api-so.h"
#include "afb-api-dbus.h"
#include "afb-api-ws.h"
#include "afb-hsrv.h"
#include "afb-context.h"
#include "afb-hreq.h"
#include "afb-sig-handler.h"
#include "afb-thread.h"
#include "session.h"
#include "verbose.h"
#include "afb-common.h"
#include "afb-hook.h"

#include <afb/afb-binding.h>

#if !defined(BINDING_INSTALL_DIR)
#error "you should define BINDING_INSTALL_DIR"
#endif

#define TRACEREQ_NO     0
#define TRACEREQ_COMMON 1
#define TRACEREQ_EXTRA  2
#define TRACEREQ_ALL    3

#define AFB_VERSION    "0.5"

// Define command line option
#define SET_VERBOSE        1
#define SET_BACKGROUND     2
#define SET_FORGROUND      3

#define SET_TCP_PORT       5
#define SET_ROOT_DIR       6
#define SET_ROOT_BASE      7
#define SET_ROOT_API       8
#define SET_ALIAS          9

#define SET_CACHE_TIMEOUT  10
#define SET_SESSION_DIR    11

#define SET_AUTH_TOKEN     12
#define SET_LDPATH         13
#define SET_APITIMEOUT     14
#define SET_CNTXTIMEOUT    15

#define DISPLAY_VERSION    16
#define DISPLAY_HELP       17

#define SET_MODE           18
#define SET_READYFD        19

#define DBUS_CLIENT        20
#define DBUS_SERVICE       21
#define SO_BINDING         22

#define SET_SESSIONMAX     23

#define WS_CLIENT          24
#define WS_SERVICE         25

#define SET_ROOT_HTTP      26

#define SET_TRACEREQ       27

// Command line structure hold cli --command + help text
typedef struct {
  int  val;        // command number within application
  int  has_arg;    // command number within application
  char *name;      // command as used in --xxxx cli
  char *help;      // help text
} AFB_options;


// Supported option
static  AFB_options cliOptions [] = {
  {SET_VERBOSE      ,0,"verbose"         , "Verbose Mode, repeat to increase verbosity"},

  {SET_FORGROUND    ,0,"foreground"      , "Get all in foreground mode"},
  {SET_BACKGROUND   ,0,"daemon"          , "Get all in background mode"},

  {SET_TCP_PORT     ,1,"port"            , "HTTP listening TCP port  [default 1234]"},
  {SET_ROOT_DIR     ,1,"rootdir"         , "Root Directory [default $HOME/.AFB]"},
  {SET_ROOT_HTTP    ,1,"roothttp"        , "HTTP Root Directory [default rootdir]"},
  {SET_ROOT_BASE    ,1,"rootbase"        , "Angular Base Root URL [default /opa]"},
  {SET_ROOT_API     ,1,"rootapi"         , "HTML Root API URL [default /api]"},
  {SET_ALIAS        ,1,"alias"           , "Muliple url map outside of rootdir [eg: --alias=/icons:/usr/share/icons]"},

  {SET_APITIMEOUT   ,1,"apitimeout"      , "Binding API timeout in seconds [default 10]"},
  {SET_CNTXTIMEOUT  ,1,"cntxtimeout"     , "Client Session Context Timeout [default 900]"},
  {SET_CACHE_TIMEOUT,1,"cache-eol"       , "Client cache end of live [default 3600]"},

  {SET_SESSION_DIR  ,1,"sessiondir"      , "Sessions file path [default rootdir/sessions]"},

  {SET_LDPATH       ,1,"ldpaths"         , "Load bindingss from dir1:dir2:... [default = "BINDING_INSTALL_DIR"]"},
  {SET_AUTH_TOKEN   ,1,"token"           , "Initial Secret [default=no-session, --token="" for session without authentication]"},

  {DISPLAY_VERSION  ,0,"version"         , "Display version and copyright"},
  {DISPLAY_HELP     ,0,"help"            , "Display this help"},

  {SET_MODE         ,1,"mode"            , "set the mode: either local, remote or global"},
  {SET_READYFD      ,1,"readyfd"         , "set the #fd to signal when ready"},

  {DBUS_CLIENT      ,1,"dbus-client"     , "bind to an afb service through dbus"},
  {DBUS_SERVICE     ,1,"dbus-server"     , "provides an afb service through dbus"},
  {WS_CLIENT        ,1,"ws-client"       , "bind to an afb service through websocket"},
  {WS_SERVICE       ,1,"ws-server"       , "provides an afb service through websockets"},
  {SO_BINDING       ,1,"binding"         , "load the binding of path"},

  {SET_SESSIONMAX   ,1,"session-max"     , "max count of session simultaneously [default 10]"},

  {SET_TRACEREQ     ,1,"tracereq"        , "log the requests: no, common, extra, all"},

  {0, 0, NULL, NULL}
 };

/*----------------------------------------------------------
 | printversion
 |   print version and copyright
 +--------------------------------------------------------- */
static void printVersion (FILE *file)
{
   fprintf(file, "\n----------------------------------------- \n");
   fprintf(file, "  AFB [Application Framework Binder] version=%s |\n", AFB_VERSION);
   fprintf(file, " \n");
   fprintf(file, "  Copyright (C) 2015, 2016 \"IoT.bzh\" [fulup -at- iot.bzh]\n");
   fprintf(file, "  AFB comes with ABSOLUTELY NO WARRANTY.\n");
   fprintf(file, "  Licence Apache 2\n\n");
   exit (0);
}

/*----------------------------------------------------------
 | printHelp
 |   print information from long option array
 +--------------------------------------------------------- */

static void printHelp(FILE *file, const char *name)
{
    int ind;
    char command[50];

    fprintf (file, "%s:\nallowed options\n", name);
    for (ind=0; cliOptions [ind].name != NULL;ind++)
    {
      // display options
      if (cliOptions [ind].has_arg == 0 )
      {
	     fprintf (file, "  --%-15s %s\n", cliOptions [ind].name, cliOptions[ind].help);
      } else {
         sprintf(command, "%s=xxxx", cliOptions [ind].name);
         fprintf (file, "  --%-15s %s\n", command, cliOptions[ind].help);
      }
    }
    fprintf (file, "Example:\n  %s\\\n  --verbose --port=1234 --token='azerty' --ldpaths=build/bindings:/usr/lib64/agl/bindings\n", name);
}

// load config from disk and merge with CLI option
static void config_set_default (struct afb_config * config)
{
   // default HTTP port
   if (config->httpdPort == 0)
	config->httpdPort = 1234;

   // default binding API timeout
   if (config->apiTimeout == 0)
	config->apiTimeout = DEFLT_API_TIMEOUT;

   // default AUTH_TOKEN
   if (config->token == NULL)
		config->token = DEFLT_AUTH_TOKEN;

   // cache timeout default one hour
   if (config->cacheTimeout == 0)
		config->cacheTimeout = DEFLT_CACHE_TIMEOUT;

   // cache timeout default one hour
   if (config->cntxTimeout == 0)
		config->cntxTimeout = DEFLT_CNTX_TIMEOUT;

   // max count of sessions
   if (config->nbSessionMax == 0)
       config->nbSessionMax = CTX_NBCLIENTS;

   if (config->rootdir == NULL) {
       config->rootdir = getenv("AFBDIR");
       if (config->rootdir == NULL) {
           config->rootdir = malloc (512);
           strncpy (config->rootdir, getenv("HOME"),512);
           strncat (config->rootdir, "/.AFB",512);
       }
       // if directory does not exist createit
       mkdir (config->rootdir,  O_RDWR | S_IRWXU | S_IRGRP);
   }

   // if no Angular/HTML5 rootbase let's try '/' as default
   if  (config->roothttp == NULL)
       config->roothttp = ".";

   if  (config->rootbase == NULL)
       config->rootbase = "/opa";

   if  (config->rootapi == NULL)
       config->rootapi = "/api";

   if  (config->ldpaths == NULL)
       config->ldpaths = BINDING_INSTALL_DIR;

   // if no session dir create a default path from rootdir
   if  (config->sessiondir == NULL) {
       config->sessiondir = malloc (512);
       strncpy (config->sessiondir, config->rootdir, 512);
       strncat (config->sessiondir, "/sessions",512);
   }

   // if no config dir create a default path from sessiondir
   if  (config->console == NULL) {
       config->console = malloc (512);
       strncpy (config->console, config->sessiondir, 512);
       strncat (config->console, "/AFB-console.out",512);
   }
}


/*---------------------------------------------------------
 | main
 |   Parse option and launch action
 +--------------------------------------------------------- */

static void add_item(struct afb_config *config, int kind, char *value)
{
	struct afb_config_item *item = malloc(sizeof *item);
	if (item == NULL) {
		ERROR("out of memory");
		exit(1);
	}
	item->kind = kind;
	item->value = value;
	item->previous = config->items;
	config->items = item;
}

static void parse_arguments(int argc, char *argv[], struct afb_config *config)
{
  char*          programName = argv [0];
  int            optionIndex = 0;
  int            optc, ind;
  int            nbcmd;
  struct option *gnuOptions;

  // ------------------ Process Command Line -----------------------

  // if no argument print help and return
  if (argc < 2) {
       printHelp(stderr, programName);
       exit(1);
  }

  // build GNU getopt info from cliOptions
  nbcmd = sizeof (cliOptions) / sizeof (AFB_options);
  gnuOptions = malloc (sizeof (*gnuOptions) * (unsigned)nbcmd);
  for (ind=0; ind < nbcmd;ind++) {
    gnuOptions [ind].name    = cliOptions[ind].name;
    gnuOptions [ind].has_arg = cliOptions[ind].has_arg;
    gnuOptions [ind].flag    = 0;
    gnuOptions [ind].val     = cliOptions[ind].val;
  }

  // get all options from command line
  while ((optc = getopt_long (argc, argv, "vsp?", gnuOptions, &optionIndex))
        != EOF)
  {
    switch (optc)
    {
     case SET_VERBOSE:
       verbosity++;
       break;

    case SET_TCP_PORT:
       if (optarg == 0) goto needValueForOption;
       if (!sscanf (optarg, "%d", &config->httpdPort)) goto notAnInteger;
       break;

    case SET_APITIMEOUT:
       if (optarg == 0) goto needValueForOption;
       if (!sscanf (optarg, "%d", &config->apiTimeout)) goto notAnInteger;
       break;

    case SET_CNTXTIMEOUT:
       if (optarg == 0) goto needValueForOption;
       if (!sscanf (optarg, "%d", &config->cntxTimeout)) goto notAnInteger;
       break;

    case SET_ROOT_DIR:
       if (optarg == 0) goto needValueForOption;
       config->rootdir   = optarg;
       INFO("Forcing Rootdir=%s",config->rootdir);
       break;

    case SET_ROOT_HTTP:
       if (optarg == 0) goto needValueForOption;
       config->roothttp   = optarg;
       INFO("Forcing Root HTTP=%s",config->roothttp);
       break;

    case SET_ROOT_BASE:
       if (optarg == 0) goto needValueForOption;
       config->rootbase   = optarg;
       INFO("Forcing Rootbase=%s",config->rootbase);
       break;

    case SET_ROOT_API:
       if (optarg == 0) goto needValueForOption;
       config->rootapi   = optarg;
       INFO("Forcing Rootapi=%s",config->rootapi);
       break;

    case SET_ALIAS:
       if (optarg == 0) goto needValueForOption;
       if ((unsigned)config->aliascount < sizeof (config->aliasdir) / sizeof (config->aliasdir[0])) {
            config->aliasdir[config->aliascount].url  = strsep(&optarg,":");
            if (optarg == NULL) {
              ERROR("missing ':' in alias %s, ignored", config->aliasdir[config->aliascount].url);
            } else {
              config->aliasdir[config->aliascount].path = optarg;
              INFO("Alias url=%s path=%s", config->aliasdir[config->aliascount].url, config->aliasdir[config->aliascount].path);
              config->aliascount++;
            }
       } else {
           ERROR("Too many aliases [max:%d] %s ignored", MAX_ALIAS, optarg);
       }
       break;

    case SET_AUTH_TOKEN:
       if (optarg == 0) goto needValueForOption;
       config->token   = optarg;
       break;

    case SET_LDPATH:
       if (optarg == 0) goto needValueForOption;
       config->ldpaths = optarg;
       break;

    case SET_SESSION_DIR:
       if (optarg == 0) goto needValueForOption;
       config->sessiondir   = optarg;
       break;

    case  SET_CACHE_TIMEOUT:
       if (optarg == 0) goto needValueForOption;
       if (!sscanf (optarg, "%d", &config->cacheTimeout)) goto notAnInteger;
       break;

    case  SET_SESSIONMAX:
       if (optarg == 0) goto needValueForOption;
       if (!sscanf (optarg, "%d", &config->nbSessionMax)) goto notAnInteger;
       break;

    case SET_FORGROUND:
       if (optarg != 0) goto noValueForOption;
       config->background  = 0;
       break;

    case SET_BACKGROUND:
       if (optarg != 0) goto noValueForOption;
       config->background  = 1;
       break;

    case SET_MODE:
       if (optarg == 0) goto needValueForOption;
       if (!strcmp(optarg, "local")) config->mode = AFB_MODE_LOCAL;
       else if (!strcmp(optarg, "remote")) config->mode = AFB_MODE_REMOTE;
       else if (!strcmp(optarg, "global")) config->mode = AFB_MODE_GLOBAL;
       else goto badMode;
       break;

    case SET_READYFD:
       if (optarg == 0) goto needValueForOption;
       if (!sscanf (optarg, "%u", &config->readyfd)) goto notAnInteger;
       break;

    case DBUS_CLIENT:
    case DBUS_SERVICE:
    case WS_CLIENT:
    case WS_SERVICE:
    case SO_BINDING:
       if (optarg == 0) goto needValueForOption;
       add_item(config, optc, optarg);
       break;

    case SET_TRACEREQ:
       if (optarg == 0) goto needValueForOption;
       if (!strcmp(optarg, "no")) config->tracereq = TRACEREQ_NO;
       else if (!strcmp(optarg, "common")) config->tracereq = TRACEREQ_COMMON;
       else if (!strcmp(optarg, "extra")) config->tracereq = TRACEREQ_EXTRA;
       else if (!strcmp(optarg, "all")) config->tracereq = TRACEREQ_ALL;
       else goto badMode;
       break;

    case DISPLAY_VERSION:
       if (optarg != 0) goto noValueForOption;
       printVersion(stdout);
       break;

    case DISPLAY_HELP:
     default:
       printHelp(stdout, programName);
       exit(0);
    }
  }
  free(gnuOptions);

  config_set_default  (config);
  return;


needValueForOption:
  ERROR("AFB-daemon option [--%s] need a value i.e. --%s=xxx"
          ,gnuOptions[optionIndex].name, gnuOptions[optionIndex].name);
  exit (1);

notAnInteger:
  ERROR("AFB-daemon option [--%s] requirer an interger i.e. --%s=9"
          ,gnuOptions[optionIndex].name, gnuOptions[optionIndex].name);
  exit (1);

noValueForOption:
  ERROR("AFB-daemon option [--%s] don't take value"
          ,gnuOptions[optionIndex].name);
  exit (1);

badMode:
  ERROR("AFB-daemon option [--%s] only accepts local, global or remote."
          ,gnuOptions[optionIndex].name);
  exit (1);
}

/*----------------------------------------------------------
 | closeSession
 |   try to close everything before leaving
 +--------------------------------------------------------- */
static void closeSession (int status, void *data) {
	/* struct afb_config *config = data; */
}

/*----------------------------------------------------------
 | daemonize
 |   set the process in background
 +--------------------------------------------------------- */
static void daemonize(struct afb_config *config)
{
  int            consoleFD;
  int            pid;

      // open /dev/console to redirect output messAFBes
      consoleFD = open(config->console, O_WRONLY | O_APPEND | O_CREAT , 0640);
      if (consoleFD < 0) {
  		ERROR("AFB-daemon cannot open /dev/console (use --foreground)");
  		exit (1);
      }

      // fork process when running background mode
      pid = fork ();

      // if fail nothing much to do
      if (pid == -1) {
  		ERROR("AFB-daemon Failed to fork son process");
  		exit (1);
	}

      // if in father process, just leave
      if (pid != 0) _exit (0);

      // son process get all data in standalone mode
     NOTICE("background mode [pid:%d console:%s]", getpid(),config->console);

      // redirect default I/O on console
      close (2); dup(consoleFD);  // redirect stderr
      close (1); dup(consoleFD);  // redirect stdout
      close (0);           // no need for stdin
      close (consoleFD);

#if 0
  	 setsid();   // allow father process to fully exit
     sleep (2);  // allow main to leave and release port
#endif
}

/*---------------------------------------------------------
 | http server
 |   Handles the HTTP server
 +--------------------------------------------------------- */
static int init_http_server(struct afb_hsrv *hsrv, struct afb_config * config)
{
	int idx, dfd;

	dfd = afb_common_rootdir_get_fd();

	if (!afb_hsrv_add_handler(hsrv, config->rootapi, afb_hswitch_websocket_switch, NULL, 20))
		return 0;

	if (!afb_hsrv_add_handler(hsrv, config->rootapi, afb_hswitch_apis, NULL, 10))
		return 0;

	for (idx = 0; idx < config->aliascount; idx++)
		if (!afb_hsrv_add_alias (hsrv, config->aliasdir[idx].url, dfd, config->aliasdir[idx].path, 0, 0))
			return 0;

	if (!afb_hsrv_add_alias(hsrv, "", dfd, config->roothttp, -10, 1))
		return 0;

	if (!afb_hsrv_add_handler(hsrv, config->rootbase, afb_hswitch_one_page_api_redirect, NULL, -20))
		return 0;

	return 1;
}

static struct afb_hsrv *start_http_server(struct afb_config * config)
{
	int rc;
	struct afb_hsrv *hsrv;

	if (afb_hreq_init_download_path("/tmp")) { /* TODO: sessiondir? */
		ERROR("unable to set the tmp directory");
		return NULL;
	}

	hsrv = afb_hsrv_create();
	if (hsrv == NULL) {
		ERROR("memory allocation failure");
		return NULL;
	}

	if (!afb_hsrv_set_cache_timeout(hsrv, config->cacheTimeout)
	|| !init_http_server(hsrv, config)) {
		ERROR("initialisation of httpd failed");
		afb_hsrv_put(hsrv);
		return NULL;
	}

	NOTICE("Waiting port=%d rootdir=%s", config->httpdPort, config->rootdir);
	NOTICE("Browser URL= http:/*localhost:%d", config->httpdPort);

	rc = afb_hsrv_start(hsrv, (uint16_t) config->httpdPort, 15);
	if (!rc) {
		ERROR("starting of httpd failed");
		afb_hsrv_put(hsrv);
		return NULL;
	}

	return hsrv;
}

static void start_items(struct afb_config_item *item)
{
  if (item != NULL) {
    /* keeps the order */
    start_items(item->previous);
    switch(item->kind) {
    case DBUS_CLIENT:
      if (afb_api_dbus_add_client(item->value) < 0) {
        ERROR("can't start the afb-dbus client of path %s",item->value);
	exit(1);
      }
      break;
    case DBUS_SERVICE:
      if (afb_api_dbus_add_server(item->value) < 0) {
        ERROR("can't start the afb-dbus service of path %s",item->value);
	exit(1);
      }
      break;
    case WS_CLIENT:
      if (afb_api_ws_add_client(item->value) < 0) {
        ERROR("can't start the afb-websocket client of path %s",item->value);
	exit(1);
      }
      break;
    case WS_SERVICE:
      if (afb_api_ws_add_server(item->value) < 0) {
        ERROR("can't start the afb-websocket service of path %s",item->value);
	exit(1);
      }
      break;
    case SO_BINDING:
      if (afb_api_so_add_binding(item->value) < 0) {
        ERROR("can't start the binding of path %s",item->value);
	exit(1);
      }
      break;
    default:
      ERROR("unexpected internal error");
      exit(1);
    }
    /* frre the item */
    free(item);
  }
}

/*---------------------------------------------------------
 | main
 |   Parse option and launch action
 +--------------------------------------------------------- */

int main(int argc, char *argv[])  {
  struct afb_hsrv *hsrv;
  struct afb_config *config;
  struct sd_event *eventloop;

  LOGAUTH("afb-daemon");

  // ------------- Build session handler & init config -------
  config = calloc (1, sizeof (struct afb_config));

  on_exit(closeSession, config);
  parse_arguments(argc, argv, config);

  // ------------------ sanity check ----------------------------------------
  if (config->httpdPort <= 0) {
     ERROR("no port is defined");
     exit (1);
  }

  afb_api_so_set_timeout(config->apiTimeout);
  if (config->ldpaths) {
    if (afb_api_so_add_pathset(config->ldpaths) < 0) {
      ERROR("initialisation of bindings within %s failed", config->ldpaths);
      exit(1);
    }
  }

  start_items(config->items);
  config->items = NULL;

  ctxStoreInit(config->nbSessionMax, config->cntxTimeout, config->token, afb_apis_count());
  if (!afb_hreq_init_cookie(config->httpdPort, config->rootapi, DEFLT_CNTX_TIMEOUT)) {
     ERROR("initialisation of cookies failed");
     exit (1);
  }

  if (afb_sig_handler_init() < 0) {
     ERROR("failed to initialise signal handlers");
     return 1;
  }

  if (afb_common_rootdir_set(config->rootdir) < 0) {
     ERROR("failed to set common root directory");
     return 1;
  }

  if (afb_thread_init(3, 1, 20) < 0) {
     ERROR("failed to initialise threading");
     return 1;
  }

  // let's run this program with a low priority
  nice (20);

  // ------------------ Finaly Process Commands -----------------------------
  // let's not take the risk to run as ROOT
  //if (getuid() == 0)  goto errorNoRoot;

  DEBUG("Init config done");

  // --------- run -----------
  if (config->background) {
      // --------- in background mode -----------
      INFO("entering background mode");
      daemonize(config);
  } else {
      // ---- in foreground mode --------------------
      INFO("entering foreground mode");
  }

  /* ignore any SIGPIPE */
  signal(SIGPIPE, SIG_IGN);

  /* install trace of requests */
  switch(config->tracereq) {
  default:
  case TRACEREQ_NO:
	break;
  case TRACEREQ_COMMON:
	afb_hook_req_create(NULL, NULL, NULL, afb_hook_flags_req_common, NULL, NULL);
	break;
  case TRACEREQ_EXTRA:
	afb_hook_req_create(NULL, NULL, NULL, afb_hook_flags_req_extra, NULL, NULL);
	break;
  case TRACEREQ_ALL:
	afb_hook_req_create(NULL, NULL, NULL, afb_hook_flags_req_all, NULL, NULL);
	break;
  }

   /* start the HTTP server */
   hsrv = start_http_server(config);
   if (hsrv == NULL)
	exit(1);

   /* start the services */
   if (afb_apis_start_all_services(1) < 0)
	exit(1);

   if (config->readyfd != 0) {
		static const char readystr[] = "READY=1";
		write(config->readyfd, readystr, sizeof(readystr) - 1);
		close(config->readyfd);
  }

   // infinite loop
  eventloop = afb_common_get_event_loop();
  for(;;)
    sd_event_run(eventloop, 30000000);

  WARNING("hoops returned from infinite loop [report bug]");

  return 0;
}

