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
 */

/* 
 * File:   main.c
 * Author: "Fulup Ar Foll"
 *
 * Created on 05 December 2015, 15:38
 */

#include "local-def.h"

#include <syslog.h>
#include <setjmp.h>
#include <signal.h>
#include <getopt.h>
#include <pwd.h>

static sigjmp_buf exitPoint; // context save for set/longjmp

/*----------------------------------------------------------
 | printversion
 |   print version and copyright
 +--------------------------------------------------------- */
 static void printVersion (void) {

   fprintf (stderr,"\n----------------------------------------- \n");
   fprintf (stderr,"|  AFB [Application Framework Binder] version=%s |\n", AJQ_VERSION);
   fprintf (stderr,"----------------------------------------- \n");
   fprintf (stderr,"|  Copyright(C) 2015 Fulup Ar Foll /IoT.bzh [fulup -at- iot.bzh]\n");
   fprintf (stderr,"|  AFB comes with ABSOLUTELY NO WARRANTY.\n");
   fprintf (stderr,"|  Licence [what ever makes you happy] until you fix bugs by yourself :)\n\n");
   exit (0);
 } // end printVersion


// Define command line option
#define SET_VERBOSE        101
#define SET_BACKGROUND     105
#define SET_FORGROUND      106
#define KILL_PREV_EXIT     107
#define KILL_PREV_REST     108
#define SET_FAKE_MOD       109

#define SET_TCP_PORT       120
#define SET_ROOT_DIR       121
#define SET_ROOT_BASE      122
#define SET_ROOT_API       123
#define SET_ROOT_ALIAS     124

#define SET_CACHE_TO       130
#define SET_USERID         131
#define SET_PID_FILE       132
#define SET_SESSION_DIR    133
#define SET_CONFIG_FILE    134
#define SET_CONFIG_SAVE    135
#define SET_CONFIG_EXIT    138

#define SET_AUTH_TOKEN     141
#define SET_LDPATH         142
#define SET_APITIMEOUT     143
#define SET_CNTXTIMEOUT    144

#define DISPLAY_VERSION    150
#define DISPLAY_HELP       151

#define SET_MODE           160
#define SET_READYFD        161


// Supported option
static  AFB_options cliOptions [] = {
  {SET_VERBOSE      ,0,"verbose"         , "Verbose Mode"},

  {SET_FORGROUND    ,0,"foreground"      , "Get all in foreground mode"},
  {SET_BACKGROUND   ,0,"daemon"          , "Get all in background mode"},
  {KILL_PREV_EXIT   ,0,"kill"            , "Kill active process if any and exit"},
  {KILL_PREV_REST   ,0,"restart"         , "Kill active process if any and restart"},

  {SET_TCP_PORT     ,1,"port"            , "HTTP listening TCP port  [default 1234]"},
  {SET_ROOT_DIR     ,1,"rootdir"         , "HTTP Root Directory [default $HOME/.AFB]"},
  {SET_ROOT_BASE    ,1,"rootbase"        , "Angular Base Root URL [default /opa]"},
  {SET_ROOT_API     ,1,"rootapi"         , "HTML Root API URL [default /api]"},
  {SET_ROOT_ALIAS   ,1,"alias"           , "Muliple url map outside of rootdir [eg: --alias=/icons:/usr/share/icons]"},
  
  {SET_APITIMEOUT   ,1,"apitimeout"      , "Plugin API timeout in seconds [default 10]"},
  {SET_CNTXTIMEOUT  ,1,"cntxtimeout"     , "Client Session Context Timeout [default 900]"},
  {SET_CACHE_TO     ,1,"cache-eol"       , "Client cache end of live [default 3600s]"},
  
  {SET_USERID       ,1,"setuid"          , "Change user id [default don't change]"},
  {SET_PID_FILE     ,1,"pidfile"         , "PID file path [default none]"},
  {SET_SESSION_DIR  ,1,"sessiondir"      , "Sessions file path [default rootdir/sessions]"},
  {SET_CONFIG_FILE  ,1,"config"          , "Config Filename [default rootdir/sessions/configs/default.AFB]"},
  {SET_CONFIG_SAVE  ,0,"save"            , "Save config on disk [default no]"},
  {SET_CONFIG_EXIT  ,0,"saveonly"        , "Save config on disk and then exit"},

  {SET_LDPATH       ,1,"ldpaths"         , "Load Plugins from dir1:dir2:... [default = PLUGIN_INSTALL_DIR"},
  {SET_AUTH_TOKEN   ,1,"token"           , "Initial Secret [default=no-session, --token="" for session without authentication]"},
  
  {DISPLAY_VERSION  ,0,"version"         , "Display version and copyright"},
  {DISPLAY_HELP     ,0,"help"            , "Display this help"},

  {SET_MODE         ,1,"mode"            , "set the mode: either local, remote or global"},
  {SET_READYFD      ,1,"readyfd"         , "set the #fd to signal when ready"},
  {0, 0, 0}
 };

static AFB_aliasdir aliasdir[MAX_ALIAS];
static int aliascount=0;

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

  fprintf (stderr, "%s ERR:Received signal quit\n",configTime());
  syslog (LOG_ERR, "Daemon got kill3 & quit [please report bug]");
  longjmp (exitPoint, signum);
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

/*----------------------------------------------------------
 | writePidFile
 |   write a file in /var/run/AFB with pid
 +--------------------------------------------------------- */
static int writePidFile (AFB_config *config, int pid) {
  FILE *file;

  // if no pid file configure just return
  if (config->pidfile == NULL) return 0;

  // open pid file in write mode
  file = fopen(config->pidfile,"w");
  if (file == NULL) {
    fprintf (stderr,"%s ERR:writePidFile fail to open [%s]\n",configTime(), config->pidfile);
    return -1;
  }

  // write pid in file and close
  fprintf (file, "%d\n", pid);
  fclose  (file);
  return 0;
}

/*----------------------------------------------------------
 | readPidFile
 |   read file in /var/run/AFB with pid
 +--------------------------------------------------------- */
static int readPidFile (AFB_config *config) {
  int  pid;
  FILE *file;
  int  status;

  if (config->pidfile == NULL) return -1;

  // open pid file in write mode
  file = fopen(config->pidfile,"r");
  if (file == NULL) {
    fprintf (stderr,"%s ERR:readPidFile fail to open [%s]\n",configTime(), config->pidfile);
    return -1;
  }

  // write pid in file and close
  status = fscanf  (file, "%d\n", &pid);
  fclose  (file);

  // never kill pid 0
  if (status != 1) return -1;

  return (pid);
}

/*----------------------------------------------------------
 | closeSession
 |   try to close everything before leaving
 +--------------------------------------------------------- */
static void closeSession (AFB_session *session) {


}

/*----------------------------------------------------------
 | listenLoop
 |   Main listening HTTP loop
 +--------------------------------------------------------- */
static void listenLoop (AFB_session *session) {
  AFB_error  err;

  if (signal (SIGABRT, signalQuit) == SIG_ERR) {
        fprintf (stderr, "%s ERR: main fail to install Signal handler\n", configTime());
        return;
  }

  // ------ Start httpd server
  if (session->config->httpdPort > 0) {

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
}
  
/*---------------------------------------------------------
 | main
 |   Parse option and launch action
 +--------------------------------------------------------- */

int main(int argc, char *argv[])  {
  AFB_session    *session;
  char*          programName = argv [0];
  int            optionIndex = 0;
  int            optc, ind, consoleFD;
  int            pid, nbcmd, status;
  AFB_config     cliconfig; // temp structure to store CLI option before file config upload

  // ------------- Build session handler & init config -------
  session = configInit ();
  memset(&cliconfig,0,sizeof(cliconfig));
  memset(&aliasdir  ,0,sizeof(aliasdir));
  cliconfig.aliasdir = aliasdir;

  // GNU CLI getopts nterface.
  struct option ggcOption;
  struct option *gnuOptions;

  // ------------------ Process Command Line -----------------------

  // if no argument print help and return
  if (argc < 2) {
       printHelp(programName);
       return (-1);
  }

  // build GNU getopt info from cliOptions
  nbcmd = sizeof (cliOptions) / sizeof (AFB_options);
  gnuOptions = malloc (sizeof (ggcOption) * nbcmd);
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
       if (!sscanf (optarg, "%d", &cliconfig.httpdPort)) goto notAnInteger;
       break;
       
    case SET_APITIMEOUT:
       if (optarg == 0) goto needValueForOption;
       if (!sscanf (optarg, "%d", &cliconfig.apiTimeout)) goto notAnInteger;
       break;

    case SET_CNTXTIMEOUT:
       if (optarg == 0) goto needValueForOption;
       if (!sscanf (optarg, "%d", &cliconfig.cntxTimeout)) goto notAnInteger;
       break;

    case SET_ROOT_DIR:
       if (optarg == 0) goto needValueForOption;
       cliconfig.rootdir   = optarg;
       if (verbose) fprintf(stderr, "Forcing Rootdir=%s\n",cliconfig.rootdir);
       break;       
       
    case SET_ROOT_BASE:
       if (optarg == 0) goto needValueForOption;
       cliconfig.rootbase   = optarg;
       if (verbose) fprintf(stderr, "Forcing Rootbase=%s\n",cliconfig.rootbase);
       break;

    case SET_ROOT_API:
       if (optarg == 0) goto needValueForOption;
       cliconfig.rootapi   = optarg;
       if (verbose) fprintf(stderr, "Forcing Rootapi=%s\n",cliconfig.rootapi);
       break;
       
    case SET_ROOT_ALIAS:
       if (optarg == 0) goto needValueForOption;
       if (aliascount < MAX_ALIAS) {
            aliasdir[aliascount].url  = strsep(&optarg,":");
            aliasdir[aliascount].path = strsep(&optarg,":");
            aliasdir[aliascount].len  = strlen(aliasdir[aliascount].url);
            if (verbose) fprintf(stderr, "Alias url=%s path=%s\n", aliasdir[aliascount].url, aliasdir[aliascount].path);
            aliascount++;
       } else {
           fprintf(stderr, "Too many aliases [max:%s] %s ignored\n", optarg, MAX_ALIAS-1);
       }     
       break;
       
    case SET_AUTH_TOKEN:
       if (optarg == 0) goto needValueForOption;
       cliconfig.token   = optarg;
       break;

    case SET_LDPATH:
       if (optarg == 0) goto needValueForOption;
       cliconfig.ldpaths = optarg;
       break;

    case SET_PID_FILE:
       if (optarg == 0) goto needValueForOption;
       cliconfig.pidfile   = optarg;
       break;

    case SET_SESSION_DIR:
       if (optarg == 0) goto needValueForOption;
       cliconfig.sessiondir   = optarg;
       break;

    case  SET_CONFIG_FILE:
       if (optarg == 0) goto needValueForOption;
       cliconfig.configfile   = optarg;
       break;

    case  SET_CACHE_TO:
       if (optarg == 0) goto needValueForOption;
       if (!sscanf (optarg, "%d", &cliconfig.cacheTimeout)) goto notAnInteger;
       break;

    case SET_CONFIG_EXIT:
       if (optarg != 0) goto noValueForOption;
       session->configsave  = 1;
       session->forceexit   = 1;
       break;

    case SET_CONFIG_SAVE:
       if (optarg != 0) goto noValueForOption;
       session->configsave  = 1;
       break;

    case SET_USERID:
       if (optarg == 0) goto needValueForOption;
       if (!sscanf (optarg, "%s", &cliconfig.setuid)) goto notAnInteger;
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

     case KILL_PREV_REST:
       if (optarg != 0) goto noValueForOption;
       session->killPrevious  = 1;
       break;

     case KILL_PREV_EXIT:
       if (optarg != 0) goto noValueForOption;
       session->killPrevious  = 2;
       break;

    case SET_MODE:
       if (optarg == 0) goto needValueForOption;
       if (!strcmp(optarg, "local")) cliconfig.mode = AFB_MODE_LOCAL;
       else if (!strcmp(optarg, "remote")) cliconfig.mode = AFB_MODE_REMOTE;
       else if (!strcmp(optarg, "global")) cliconfig.mode = AFB_MODE_GLOBAL;
       else goto badMode;
       break;

    case SET_READYFD:
       if (optarg == 0) goto needValueForOption;
       if (!sscanf (optarg, "%u", &session->readyfd)) goto notAnInteger;
       break;

    case DISPLAY_VERSION:
       if (optarg != 0) goto noValueForOption;
       printVersion();
       goto normalExit;

    case DISPLAY_HELP:
     default:
       printHelp(programName);
       goto normalExit;

    }
  }
 
  // if exist merge config file with CLI arguments
  configLoadFile  (session, &cliconfig);
  initPlugins(session);

  // ------------------ sanity check ----------------------------------------
  if  ((session->background) && (session->foreground)) {
    fprintf (stderr, "%s ERR: cannot select foreground & background at the same time\n",configTime());
     exit (-1);
  }

  // ------------------ Some useful default values -------------------------
  if  ((session->background == 0) && (session->foreground == 0)) session->foreground=1;

  // open syslog if ever needed
  openlog("AFB-log", 0, LOG_DAEMON);

  // -------------- Try to kill any previous process if asked ---------------------
  if (session->killPrevious) {
    pid = readPidFile (session->config);  // enforce commandline option
    switch (pid) {
    case -1:
      fprintf (stderr, "%s ERR:main --kill ignored no PID file [%s]\n",configTime(), session->config->pidfile);
      break;
    case 0:
      fprintf (stderr, "%s ERR:main --kill ignored no active AFB process\n",configTime());
      break;
    default:
      status = kill (pid,SIGINT );
      if (status == 0) {
	     if (verbose) printf ("%s INF:main signal INTR sent to pid:%d \n", configTime(), pid);
      } else {
         // try kill -9
         status = kill (pid,9);
         if (status != 0)  fprintf (stderr, "%s ERR:main failled to killed pid=%d \n",configTime(), pid);
      }
    } // end switch pid

    if (session->killPrevious >= 2) goto normalExit;
  } // end killPrevious


  // ------------------ clean exit on CTR-C signal ------------------------
  if (signal (SIGINT, signalQuit) == SIG_ERR) {
    fprintf (stderr, "%s Quit Signal received.",configTime());
    return (-1);
  }

  // save exitPoint context when returning from longjmp closeSession and exit
  status = setjmp (exitPoint); // return !+ when coming from longjmp
  if (status != 0) {
    if (verbose) printf ("INF:main returning from longjump after signal [%d]\n", status);
    closeSession (session);
    goto exitOnSignal;
  }

  // let's run this program with a low priority
  status=nice (20);

  // ------------------ Finaly Process Commands -----------------------------
   // if --save then store config on disk upfront
   if (session->configsave) configStoreFile (session);
   if (session->forceexit)  exit (0);

    if (session->config->setuid) {
        int err;
        struct passwd *passwd;
        passwd=getpwnam(session->config->setuid);
        
        if (passwd == NULL) goto errorSetuid;
        
        err = setuid(passwd->pw_uid);
        if (err) goto errorSetuid;
    }

    // let's not take the risk to run as ROOT
    //if (getuid() == 0)  goto errorNoRoot;

    // check session dir and create if it does not exist
    if (sessionCheckdir (session) != AFB_SUCCESS) goto errSessiondir;
    if (verbose) fprintf (stderr, "AFB:notice Init config done\n");



    // ---- run in foreground mode --------------------
    if (session->foreground) {

        if (verbose) fprintf (stderr,"AFB:notice Foreground mode\n");

        // write a pid file for --kill-previous and --raise-debug option
        status = writePidFile (session->config, getpid());
        if (status == -1) goto errorPidFile;

        // enter listening loop in foreground
        listenLoop(session);
        goto exitInitLoop;
  } // end foreground


  // --------- run in background mode -----------
  if (session->background) {

       // if (status != 0) goto errorCommand;
      if (verbose) printf ("AFB: Entering background mode\n");

      // open /dev/console to redirect output messAFBes
      consoleFD = open(session->config->console, O_WRONLY | O_APPEND | O_CREAT , 0640);
      if (consoleFD < 0) goto errConsole;

      // fork process when running background mode
      pid = fork ();

      // son process get all data in standalone mode
      if (pid == 0) {

 	     printf ("\nAFB: background mode [pid:%d console:%s]\n", getpid(),session->config->console);
 	     if (verbose) printf ("AFB:info use '%s --restart --rootdir=%s # [--pidfile=%s] to restart daemon\n", programName,session->config->rootdir, session->config->pidfile);

         // redirect default I/O on console
         close (2); status=dup(consoleFD);  // redirect stderr
         close (1); status=dup(consoleFD);  // redirect stdout
         close (0);           // no need for stdin
         close (consoleFD);

    	 setsid();   // allow father process to fully exit
	     sleep (2);  // allow main to leave and release port

         fprintf (stderr, "----------------------------\n");
         fprintf (stderr, "%s INF:main background pid=%d\n", configTime(), getpid());
         fflush  (stderr);

         // if everything look OK then look forever
         syslog (LOG_ERR, "AFB: Entering infinite loop in background mode");

         // should normally never return from this loop
         listenLoop(session);
         syslog (LOG_ERR, "AFB:FAIL background infinite loop exited check [%s]\n", session->config->console);

         goto exitInitLoop;
      }

      // if fail nothing much to do
      if (pid == -1) goto errorFork;

      // fork worked and we are in father process
      status = writePidFile (session->config, pid);
      if (status == -1) goto errorPidFile;

      // we are in father process, we don't need this one
      _exit (0);

  } // end background-foreground

normalExit:
  closeSession (session);   // try to close everything before leaving
  if (verbose) printf ("\n---- Application Framework Binder Normal End ------\n");
  exit(0);

// ------------- Fatal ERROR display error and quit  -------------
errorSetuid:
  fprintf (stderr,"\nERR:AFB-daemon Failed to change UID to username=[%s]\n\n", session->config->setuid);
  exit (-1);
  
//errorNoRoot:
//  fprintf (stderr,"\nERR:AFB-daemon Not allow to run as root [use --seteuid=username option]\n\n");
//  exit (-1);

errorPidFile:
  fprintf (stderr,"\nERR:AFB-daemon Failed to write pid file [%s]\n\n", session->config->pidfile);
  exit (-1);

errorFork:
  fprintf (stderr,"\nERR:AFB-daemon Failed to fork son process\n\n");
  exit (-1);

needValueForOption:
  fprintf (stderr,"\nERR:AFB-daemon option [--%s] need a value i.e. --%s=xxx\n\n"
          ,gnuOptions[optionIndex].name, gnuOptions[optionIndex].name);
  exit (-1);

noValueForOption:
  fprintf (stderr,"\nERR:AFB-daemon option [--%s] don't take value\n\n"
          ,gnuOptions[optionIndex].name);
  exit (-1);

notAnInteger:
  fprintf (stderr,"\nERR:AFB-daemon option [--%s] requirer an interger i.e. --%s=9\n\n"
          ,gnuOptions[optionIndex].name, gnuOptions[optionIndex].name);
  exit (-1);

badMode:
  fprintf (stderr,"\nERR:AFB-daemon option [--%s] only accepts local, global or remote.\n\n"
          ,gnuOptions[optionIndex].name);
  exit (-1);

exitOnSignal:
  fprintf (stderr,"\n%s INF:AFB-daemon pid=%d received exit signal (Hopefully crtl-C or --kill-previous !!!)\n\n"
                 ,configTime(), getpid());
  exit (-1);

errConsole:
  fprintf (stderr,"\nERR:AFB-daemon cannot open /dev/console (use --foreground)\n\n");
  exit (-1);

errSessiondir:
  fprintf (stderr,"\nERR:AFB-daemon cannot read/write session dir\n\n");
  exit (-1);

errSoundCard:
  fprintf (stderr,"\nERR:AFB-daemon fail to probe sound cards\n\n");
  exit (-1);

exitInitLoop:
  // try to unlink pid file if any
  if (session->background && session->config->pidfile != NULL)  unlink (session->config->pidfile);
  exit (-1);

}; /* END AFB-daemon() */

