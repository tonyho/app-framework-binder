/* 
 * Copyright (C) 2015 "IoT.bzh"
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

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <getopt.h>
#include <setjmp.h>
#include <signal.h>
#include <syslog.h>

#include <systemd/sd-event.h>

#include "afb-config.h"
#include "afb-hswitch.h"
#include "afb-apis.h"
#include "afb-api-so.h"
#include "afb-api-dbus.h"
#include "afb-hsrv.h"
#include "afb-context.h"
#include "afb-hreq.h"
#include "session.h"
#include "verbose.h"
#include "afb-common.h"

#include "afb-plugin.h"

#if !defined(PLUGIN_INSTALL_DIR)
#error "you should define PLUGIN_INSTALL_DIR"
#endif

#define AFB_VERSION    "0.4"

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
#define SO_PLUGIN          22

// Command line structure hold cli --command + help text
typedef struct {
  int  val;        // command number within application
  int  has_arg;    // command number within application
  char *name;      // command as used in --xxxx cli
  char *help;      // help text
} AFB_options;


// Supported option
static  AFB_options cliOptions [] = {
  {SET_VERBOSE      ,0,"verbose"         , "Verbose Mode"},

  {SET_FORGROUND    ,0,"foreground"      , "Get all in foreground mode"},
  {SET_BACKGROUND   ,0,"daemon"          , "Get all in background mode"},

  {SET_TCP_PORT     ,1,"port"            , "HTTP listening TCP port  [default 1234]"},
  {SET_ROOT_DIR     ,1,"rootdir"         , "HTTP Root Directory [default $HOME/.AFB]"},
  {SET_ROOT_BASE    ,1,"rootbase"        , "Angular Base Root URL [default /opa]"},
  {SET_ROOT_API     ,1,"rootapi"         , "HTML Root API URL [default /api]"},
  {SET_ALIAS        ,1,"alias"           , "Muliple url map outside of rootdir [eg: --alias=/icons:/usr/share/icons]"},
  
  {SET_APITIMEOUT   ,1,"apitimeout"      , "Plugin API timeout in seconds [default 10]"},
  {SET_CNTXTIMEOUT  ,1,"cntxtimeout"     , "Client Session Context Timeout [default 900]"},
  {SET_CACHE_TIMEOUT,1,"cache-eol"       , "Client cache end of live [default 3600s]"},
  
  {SET_SESSION_DIR  ,1,"sessiondir"      , "Sessions file path [default rootdir/sessions]"},

  {SET_LDPATH       ,1,"ldpaths"         , "Load Plugins from dir1:dir2:... [default = PLUGIN_INSTALL_DIR"},
  {SET_AUTH_TOKEN   ,1,"token"           , "Initial Secret [default=no-session, --token="" for session without authentication]"},
  
  {DISPLAY_VERSION  ,0,"version"         , "Display version and copyright"},
  {DISPLAY_HELP     ,0,"help"            , "Display this help"},

  {SET_MODE         ,1,"mode"            , "set the mode: either local, remote or global"},
  {SET_READYFD      ,1,"readyfd"         , "set the #fd to signal when ready"},

  {DBUS_CLIENT      ,1,"dbus-client"     , "bind to an afb service through dbus"},
  {DBUS_SERVICE     ,1,"dbus-server"     , "provides an afb service through dbus"},
  {SO_PLUGIN        ,1,"plugin"          , "load the plugin of path"},

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
   fprintf(file, "  Copyright(C) 2016 /IoT.bzh [fulup -at- iot.bzh]\n");
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
    fprintf (file, "Example:\n  %s\\\n  --verbose --port=1234 --token='azerty' --ldpaths=build/plugins:/usr/lib64/agl/plugins\n", name);
}

// load config from disk and merge with CLI option
static void config_set_default (struct afb_config * config)
{
   // default HTTP port
   if (config->httpdPort == 0)
	config->httpdPort = 1234;
   
   // default Plugin API timeout
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
   if  (config->rootbase == NULL)
       config->rootbase = "/opa";
   
   if  (config->rootapi == NULL)
       config->rootapi = "/api";

   if  (config->ldpaths == NULL)
       config->ldpaths = PLUGIN_INSTALL_DIR;

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
    case SO_PLUGIN:
       if (optarg == 0) goto needValueForOption;
       add_item(config, optc, optarg);
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
 | timeout signalQuit
 +--------------------------------------------------------- */
void signalQuit (int signum)
{
	ERROR("Terminating signal received %s", strsignal(signum));
	exit(1);
}

/*----------------------------------------------------------
 | Error signals
 |
 +--------------------------------------------------------- */
__thread sigjmp_buf *error_handler;
static void signalError(int signum)
{
	sigset_t sigset;

	// unlock signal to allow a new signal to come
	if (error_handler != NULL) {
		sigemptyset(&sigset);
		sigaddset(&sigset, signum);
		sigprocmask(SIG_UNBLOCK, &sigset, 0);
		longjmp(*error_handler, signum);
	}
	if (signum == SIGALRM)
		return;
	ERROR("Unmonitored signal received %s", strsignal(signum));
	exit(2);
}

static void install_error_handlers()
{
	int i, signals[] = { SIGALRM, SIGSEGV, SIGFPE, 0 };

	for (i = 0; signals[i] != 0; i++) {
		if (signal(signals[i], signalError) == SIG_ERR) {
			ERROR("Signal handler error");
			exit(1);
		}
	}
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
	int idx;

	if (!afb_hsrv_add_handler(hsrv, config->rootapi, afb_hswitch_websocket_switch, NULL, 20))
		return 0;

	if (!afb_hsrv_add_handler(hsrv, config->rootapi, afb_hswitch_apis, NULL, 10))
		return 0;

	for (idx = 0; idx < config->aliascount; idx++)
		if (!afb_hsrv_add_alias (hsrv, config->aliasdir[idx].url, config->aliasdir[idx].path, 0))
			return 0;

	if (!afb_hsrv_add_alias(hsrv, "", config->rootdir, -10))
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
    case SO_PLUGIN:
      if (afb_api_so_add_plugin(item->value) < 0) {
        ERROR("can't start the plugin of path %s",item->value);
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

  // open syslog if ever needed
  openlog("afb-daemon", 0, LOG_DAEMON);

  // ------------- Build session handler & init config -------
  config = calloc (1, sizeof (struct afb_config));

  on_exit(closeSession, config);
  parse_arguments(argc, argv, config);

  // ------------------ sanity check ----------------------------------------
  if (config->httpdPort <= 0) {
     ERROR("no port is defined");
     exit (1);
  }

  if (config->ldpaths) 
    afb_api_so_add_pathset(config->ldpaths);

  start_items(config->items);
  config->items = NULL;

  ctxStoreInit(CTX_NBCLIENTS, config->cntxTimeout, config->token, afb_apis_count());
  if (!afb_hreq_init_cookie(config->httpdPort, config->rootapi, DEFLT_CNTX_TIMEOUT)) {
     ERROR("initialisation of cookies failed");
     exit (1);
  }

  install_error_handlers();

  // ------------------ clean exit on CTR-C signal ------------------------
  if (signal (SIGINT, signalQuit) == SIG_ERR || signal (SIGABRT, signalQuit) == SIG_ERR) {
     ERROR("main fail to install Signal handler");
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

   hsrv = start_http_server(config);
   if (hsrv == NULL)
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

