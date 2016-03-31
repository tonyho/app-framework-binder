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

#include <syslog.h>
#include <setjmp.h>
#include <signal.h>
#include <getopt.h>
#include <pwd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "local-def.h"
#include "afb-apis.h"
#include "session.h"

#if !defined(PLUGIN_INSTALL_DIR)
#error "you should define PLUGIN_INSTALL_DIR"
#endif

#define AFB_VERSION    "0.1"

// Define command line option
#define SET_VERBOSE        1
#define SET_BACKGROUND     2
#define SET_FORGROUND      3
#define SET_FAKE_MOD       4

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
  {0, 0, NULL, NULL}
 };

static AFB_aliasdir aliasdir[MAX_ALIAS];
static int aliascount = 0;


/*----------------------------------------------------------
 | printversion
 |   print version and copyright
 +--------------------------------------------------------- */
static void printVersion (void)
{
   fprintf (stderr,"\n----------------------------------------- \n");
   fprintf (stderr,"|  AFB [Application Framework Binder] version=%s |\n", AFB_VERSION);
   fprintf (stderr,"----------------------------------------- \n");
   fprintf (stderr,"|  Copyright(C) 2016 /IoT.bzh [fulup -at- iot.bzh]\n");
   fprintf (stderr,"|  AFB comes with ABSOLUTELY NO WARRANTY.\n");
   fprintf (stderr,"|  Licence Apache 2\n\n");
   exit (0);
}

// load config from disk and merge with CLI option
static AFB_error config_set_default (AFB_session * session)
{
   static char cacheTimeout [10];
   
   // default HTTP port
   if (session->config->httpdPort == 0) session->config->httpdPort=1234;
   
   // default Plugin API timeout
   if (session->config->apiTimeout == 0) session->config->apiTimeout=DEFLT_API_TIMEOUT;
   
   // default AUTH_TOKEN
   if (session->config->token == NULL) session->config->token= DEFLT_AUTH_TOKEN;

   // cache timeout default one hour
   if (session->config->cacheTimeout == 0) session->config->cacheTimeout=DEFLT_CACHE_TIMEOUT;

   // cache timeout default one hour
   if (session->config->cntxTimeout == 0) session->config->cntxTimeout=DEFLT_CNTX_TIMEOUT;

   if (session->config->rootdir == NULL) {
       session->config->rootdir = getenv("AFBDIR");
       if (session->config->rootdir == NULL) {
           session->config->rootdir = malloc (512);
           strncpy  (session->config->rootdir, getenv("HOME"),512);
           strncat (session->config->rootdir, "/.AFB",512);
       }
       // if directory does not exist createit
       mkdir (session->config->rootdir,  O_RDWR | S_IRWXU | S_IRGRP);
   }
   
   // if no Angular/HTML5 rootbase let's try '/' as default
   if  (session->config->rootbase == NULL) {
       session->config->rootbase = "/opa";
   }
   
   if  (session->config->rootapi == NULL) {
       session->config->rootapi = "/api";
   }

   if  (session->config->ldpaths == NULL) {
       session->config->ldpaths = PLUGIN_INSTALL_DIR;
   }

   // if no session dir create a default path from rootdir
   if  (session->config->sessiondir == NULL) {
       session->config->sessiondir = malloc (512);
       strncpy (session->config->sessiondir, session->config->rootdir, 512);
       strncat (session->config->sessiondir, "/sessions",512);
   }

   // if no config dir create a default path from sessiondir
   if  (session->config->console == NULL) {
       session->config->console = malloc (512);
       strncpy (session->config->console, session->config->sessiondir, 512);
       strncat (session->config->console, "/AFB-console.out",512);
   }

   // cacheTimeout is an integer but HTTPd wants it as a string
   snprintf (cacheTimeout, sizeof (cacheTimeout),"%d", session->config->cacheTimeout);
   session->cacheTimeout = cacheTimeout; // httpd uses cacheTimeout string version

   return AFB_SUCCESS;
}


/*----------------------------------------------------------
 | printHelp
 |   print information from long option array
 +--------------------------------------------------------- */

 static void printHelp(char *name) {
    int ind;
    char command[20];

    fprintf (stderr,"%s:\nallowed options\n", name);
    for (ind=0; cliOptions [ind].name != NULL;ind++)
    {
      // display options
      if (cliOptions [ind].has_arg == 0 )
      {
	     fprintf (stderr,"  --%-15s %s\n", cliOptions [ind].name, cliOptions[ind].help);
      } else {
         sprintf(command,"%s=xxxx", cliOptions [ind].name);
         fprintf (stderr,"  --%-15s %s\n", command, cliOptions[ind].help);
      }
    }
    fprintf (stderr,"Example:\n  %s\\\n  --verbose --port=1234 --token='azerty' --ldpaths=build/plugins:/usr/lib64/agl/plugins\n", name);
} // end printHelp

/*---------------------------------------------------------
 | main
 |   Parse option and launch action
 +--------------------------------------------------------- */

static void parse_arguments(int argc, char *argv[], AFB_session *session)
{
  char*          programName = argv [0];
  int            optionIndex = 0;
  int            optc, ind;
  int            nbcmd;
  struct option *gnuOptions;

  // ------------- Build session handler & init config -------
  memset(&aliasdir  ,0,sizeof(aliasdir));
  session->config->aliasdir = aliasdir;

  // ------------------ Process Command Line -----------------------

  // if no argument print help and return
  if (argc < 2) {
       printHelp(programName);
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
       verbose = 1;
       break;

    case SET_TCP_PORT:
       if (optarg == 0) goto needValueForOption;
       if (!sscanf (optarg, "%d", &session->config->httpdPort)) goto notAnInteger;
       break;
       
    case SET_APITIMEOUT:
       if (optarg == 0) goto needValueForOption;
       if (!sscanf (optarg, "%d", &session->config->apiTimeout)) goto notAnInteger;
       break;

    case SET_CNTXTIMEOUT:
       if (optarg == 0) goto needValueForOption;
       if (!sscanf (optarg, "%d", &session->config->cntxTimeout)) goto notAnInteger;
       break;

    case SET_ROOT_DIR:
       if (optarg == 0) goto needValueForOption;
       session->config->rootdir   = optarg;
       if (verbose) fprintf(stderr, "Forcing Rootdir=%s\n",session->config->rootdir);
       break;       
       
    case SET_ROOT_BASE:
       if (optarg == 0) goto needValueForOption;
       session->config->rootbase   = optarg;
       if (verbose) fprintf(stderr, "Forcing Rootbase=%s\n",session->config->rootbase);
       break;

    case SET_ROOT_API:
       if (optarg == 0) goto needValueForOption;
       session->config->rootapi   = optarg;
       if (verbose) fprintf(stderr, "Forcing Rootapi=%s\n",session->config->rootapi);
       break;
       
    case SET_ALIAS:
       if (optarg == 0) goto needValueForOption;
       if (aliascount < MAX_ALIAS) {
            aliasdir[aliascount].url  = strsep(&optarg,":");
            if (optarg == NULL) {
              fprintf(stderr, "missing ':' in alias %s, ignored\n", aliasdir[aliascount].url);
            } else {
              aliasdir[aliascount].path = optarg;
              aliasdir[aliascount].len  = strlen(aliasdir[aliascount].url);
              if (verbose) fprintf(stderr, "Alias url=%s path=%s\n", aliasdir[aliascount].url, aliasdir[aliascount].path);
              aliascount++;
            }
       } else {
           fprintf(stderr, "Too many aliases [max:%d] %s ignored\n", MAX_ALIAS, optarg);
       }     
       break;
       
    case SET_AUTH_TOKEN:
       if (optarg == 0) goto needValueForOption;
       session->config->token   = optarg;
       break;

    case SET_LDPATH:
       if (optarg == 0) goto needValueForOption;
       session->config->ldpaths = optarg;
       break;

    case SET_SESSION_DIR:
       if (optarg == 0) goto needValueForOption;
       session->config->sessiondir   = optarg;
       break;

    case  SET_CACHE_TIMEOUT:
       if (optarg == 0) goto needValueForOption;
       if (!sscanf (optarg, "%d", &session->config->cacheTimeout)) goto notAnInteger;
       break;

    case SET_FAKE_MOD:
       if (optarg != 0) goto noValueForOption;
       session->fakemod  = 1;
       break;

    case SET_FORGROUND:
       if (optarg != 0) goto noValueForOption;
       session->foreground  = 1;
       break;

    case SET_BACKGROUND:
       if (optarg != 0) goto noValueForOption;
       session->background  = 1;
       break;

    case SET_MODE:
       if (optarg == 0) goto needValueForOption;
       if (!strcmp(optarg, "local")) session->config->mode = AFB_MODE_LOCAL;
       else if (!strcmp(optarg, "remote")) session->config->mode = AFB_MODE_REMOTE;
       else if (!strcmp(optarg, "global")) session->config->mode = AFB_MODE_GLOBAL;
       else goto badMode;
       break;

    case SET_READYFD:
       if (optarg == 0) goto needValueForOption;
       if (!sscanf (optarg, "%u", &session->readyfd)) goto notAnInteger;
       break;

    case DISPLAY_VERSION:
       if (optarg != 0) goto noValueForOption;
       printVersion();
       exit(0);

    case DISPLAY_HELP:
     default:
       printHelp(programName);
       exit(0);
    }
  }
  free(gnuOptions);
 
  config_set_default  (session);
  return;


needValueForOption:
  fprintf (stderr,"\nERR: AFB-daemon option [--%s] need a value i.e. --%s=xxx\n\n"
          ,gnuOptions[optionIndex].name, gnuOptions[optionIndex].name);
  exit (1);

notAnInteger:
  fprintf (stderr,"\nERR: AFB-daemon option [--%s] requirer an interger i.e. --%s=9\n\n"
          ,gnuOptions[optionIndex].name, gnuOptions[optionIndex].name);
  exit (1);

noValueForOption:
  fprintf (stderr,"\nERR: AFB-daemon option [--%s] don't take value\n\n"
          ,gnuOptions[optionIndex].name);
  exit (1);

badMode:
  fprintf (stderr,"\nERR: AFB-daemon option [--%s] only accepts local, global or remote.\n\n"
          ,gnuOptions[optionIndex].name);
  exit (1);
}

/*----------------------------------------------------------
 | closeSession
 |   try to close everything before leaving
 +--------------------------------------------------------- */
static void closeSession (int status, void *data) {
	/* AFB_session *session = data; */
}

/*----------------------------------------------------------
 | timeout signalQuit
 |
 +--------------------------------------------------------- */
void signalQuit (int signum) {

  sigset_t sigset;

  // unlock timeout signal to allow a new signal to come
  sigemptyset (&sigset);
  sigaddset   (&sigset, SIGABRT);
  sigprocmask (SIG_UNBLOCK, &sigset, 0);

  fprintf (stderr, "ERR: Received signal quit\n");
  syslog (LOG_ERR, "Daemon got kill3 & quit [please report bug]");
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
	sigemptyset(&sigset);
	sigaddset(&sigset, signum);
	sigprocmask(SIG_UNBLOCK, &sigset, 0);
	if (error_handler != NULL) {
		longjmp(*error_handler, signum);
	}
}

static void install_error_handlers()
{
	int i, signals[] = { SIGALRM, SIGSEGV, SIGFPE, 0 };

	for (i = 0; signals[i] != 0; i++) {
		if (signal(signals[i], signalError) == SIG_ERR) {
			fprintf(stderr, "Signal handler error\n");
			exit(1);
		}
	}
}

/*----------------------------------------------------------
 | listenLoop
 |   Main listening HTTP loop
 +--------------------------------------------------------- */
static void listenLoop (AFB_session *session) {
  AFB_error  err;

  // ------ Start httpd server

   err = httpdStart (session);
   if (err != AFB_SUCCESS) return;

	if (session->readyfd != 0) {
		static const char readystr[] = "READY=1";
		write(session->readyfd, readystr, sizeof(readystr) - 1);
		close(session->readyfd);
	}

   // infinite loop
   httpdLoop(session);

   fprintf (stderr, "hoops returned from infinite loop [report bug]\n");
}
  
/*----------------------------------------------------------
 | daemonize
 |   set the process in background
 +--------------------------------------------------------- */
static void daemonize(AFB_session *session)
{
  int            consoleFD;
  int            pid;

      // open /dev/console to redirect output messAFBes
      consoleFD = open(session->config->console, O_WRONLY | O_APPEND | O_CREAT , 0640);
      if (consoleFD < 0) {
  		fprintf (stderr,"\nERR: AFB-daemon cannot open /dev/console (use --foreground)\n\n");
  		exit (1);
      }

      // fork process when running background mode
      pid = fork ();

      // if fail nothing much to do
      if (pid == -1) {
  		fprintf (stderr,"\nERR: AFB-daemon Failed to fork son process\n\n");
  		exit (1);
	}

      // if in father process, just leave
      if (pid != 0) _exit (0);

      // son process get all data in standalone mode
     printf ("\nAFB: background mode [pid:%d console:%s]\n", getpid(),session->config->console);

      // redirect default I/O on console
      close (2); dup(consoleFD);  // redirect stderr
      close (1); dup(consoleFD);  // redirect stdout
      close (0);           // no need for stdin
      close (consoleFD);

#if 0
  	 setsid();   // allow father process to fully exit
     sleep (2);  // allow main to leave and release port
#endif

         fprintf (stderr, "----------------------------\n");
         fprintf (stderr, "INF: main background pid=%d\n", getpid());
         fflush  (stderr);
}

/*---------------------------------------------------------
 | main
 |   Parse option and launch action
 +--------------------------------------------------------- */

int main(int argc, char *argv[])  {
  AFB_session    *session;

  // open syslog if ever needed
  openlog("afb-daemon", 0, LOG_DAEMON);

  // ------------- Build session handler & init config -------
  session = calloc (1, sizeof (AFB_session));
  session->config = calloc (1, sizeof (AFB_config));

  on_exit(closeSession, session);
  parse_arguments(argc, argv, session);

  // ------------------ sanity check ----------------------------------------
  if  ((session->background) && (session->foreground)) {
    fprintf (stderr, "ERR: cannot select foreground & background at the same time\n");
     exit (1);
  }
  if (session->config->httpdPort <= 0) {
    fprintf (stderr, "ERR: no port is defined\n");
     exit (1);
  }

  if (session->config->ldpaths) 
    afb_apis_add_pathset(session->config->ldpaths);

  ctxStoreInit(CTX_NBCLIENTS, session->config->cntxTimeout, afb_apis_count(), session->config->token);

  install_error_handlers();

  // ------------------ Some useful default values -------------------------
  if  ((session->background == 0) && (session->foreground == 0)) session->foreground=1;

  // ------------------ clean exit on CTR-C signal ------------------------
  if (signal (SIGINT, signalQuit) == SIG_ERR || signal (SIGABRT, signalQuit) == SIG_ERR) {
     fprintf (stderr, "ERR: main fail to install Signal handler\n");
     return 1;
  }


  // let's run this program with a low priority
  nice (20);

  // ------------------ Finaly Process Commands -----------------------------
  // let's not take the risk to run as ROOT
  //if (getuid() == 0)  goto errorNoRoot;

#if defined(ALLOWS_SESSION_FILES)
  // check session dir and create if it does not exist
  if (sessionCheckdir (session) != AFB_SUCCESS) {
  	fprintf (stderr,"\nERR: AFB-daemon cannot read/write session dir\n\n");
  	exit (1);
  }
#endif
  if (verbose) fprintf (stderr, "AFB: notice Init config done\n");

  // ---- run in foreground mode --------------------
  if (session->foreground) {

        if (verbose) fprintf (stderr,"AFB: notice Foreground mode\n");

  } // end foreground

  // --------- run in background mode -----------
  if (session->background) {

      if (verbose) printf ("AFB: Entering background mode\n");

      daemonize(session);

         // if everything look OK then look forever
         syslog (LOG_ERR, "AFB: Entering infinite loop in background mode");


  } // end background-foreground


  listenLoop(session);
  if (verbose) printf ("\n---- Application Framework Binder Normal End ------\n");
  exit(0);

}


