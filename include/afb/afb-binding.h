/*
 * Copyright (C) 2016 "IoT.bzh"
 * Author: José Bollo <jose.bollo@iot.bzh>
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

#pragma once

#include <stdarg.h>

/*****************************************************************************
 * This files is the main file to include for writing bindings dedicated to
 *
 *                      AFB-DAEMON
 *
 * Functions of bindings of afb-daemon are accessible by authorized clients
 * through the apis module of afb-daemon.
 *
 * A binding is a shared library. This shared library must have at least one
 * exported symbol for being registered in afb-daemon.
 * For the current version of afb-daemon, the function exported MUST be named
 *
 *                  afbBindingV1Register
 */

/*
 * Some function of the library are exported to afb-daemon.
 */

#include <afb/afb-event-itf.h>
#include <afb/afb-req-itf.h>

/*
 * Definition of the type+versions of the binding.
 * The definition uses hashes.
 */
enum  afb_binding_type
{
       AFB_BINDING_VERSION_1 = 123456789        /* version 1 */
};

/*
 * Enum for Session/Token/Assurance middleware.
 * This enumeration is valid for bindings of type 1
 */
enum afb_session_v1
{
       AFB_SESSION_NONE = 0,   /* nothing required */
       AFB_SESSION_CREATE = 1, /* Obsolete */
       AFB_SESSION_CLOSE = 2,  /* After token authentification, closes the session at end */
       AFB_SESSION_RENEW = 4,  /* After token authentification, refreshes the token at end */
       AFB_SESSION_CHECK = 8,  /* Requires token authentification */

       AFB_SESSION_LOA_GE = 16, /* check that the LOA is greater or equal to the given value */
       AFB_SESSION_LOA_LE = 32, /* check that the LOA is lesser or equal to the given value */
       AFB_SESSION_LOA_EQ = 48, /* check that the LOA is equal to the given value */

       AFB_SESSION_LOA_SHIFT = 6, /* shift for LOA */
       AFB_SESSION_LOA_MASK = 7,  /* mask for LOA */

       AFB_SESSION_LOA_0 = 0,   /* value for LOA of 0 */
       AFB_SESSION_LOA_1 = 64,  /* value for LOA of 1 */
       AFB_SESSION_LOA_2 = 128, /* value for LOA of 2 */
       AFB_SESSION_LOA_3 = 192, /* value for LOA of 3 */
       AFB_SESSION_LOA_4 = 256, /* value for LOA of 4 */

       AFB_SESSION_LOA_LE_0 = AFB_SESSION_LOA_LE | AFB_SESSION_LOA_0, /* check LOA <= 0 */
       AFB_SESSION_LOA_LE_1 = AFB_SESSION_LOA_LE | AFB_SESSION_LOA_1, /* check LOA <= 1 */
       AFB_SESSION_LOA_LE_2 = AFB_SESSION_LOA_LE | AFB_SESSION_LOA_2, /* check LOA <= 2 */
       AFB_SESSION_LOA_LE_3 = AFB_SESSION_LOA_LE | AFB_SESSION_LOA_3, /* check LOA <= 3 */

       AFB_SESSION_LOA_GE_0 = AFB_SESSION_LOA_GE | AFB_SESSION_LOA_0, /* check LOA >= 0 */
       AFB_SESSION_LOA_GE_1 = AFB_SESSION_LOA_GE | AFB_SESSION_LOA_1, /* check LOA >= 1 */
       AFB_SESSION_LOA_GE_2 = AFB_SESSION_LOA_GE | AFB_SESSION_LOA_2, /* check LOA >= 2 */
       AFB_SESSION_LOA_GE_3 = AFB_SESSION_LOA_GE | AFB_SESSION_LOA_3, /* check LOA >= 3 */

       AFB_SESSION_LOA_EQ_0 = AFB_SESSION_LOA_EQ | AFB_SESSION_LOA_0, /* check LOA == 0 */
       AFB_SESSION_LOA_EQ_1 = AFB_SESSION_LOA_EQ | AFB_SESSION_LOA_1, /* check LOA == 1 */
       AFB_SESSION_LOA_EQ_2 = AFB_SESSION_LOA_EQ | AFB_SESSION_LOA_2, /* check LOA == 2 */
       AFB_SESSION_LOA_EQ_3 = AFB_SESSION_LOA_EQ | AFB_SESSION_LOA_3  /* check LOA == 3 */
};

/*
 * Description of one verb of the API provided by the binding
 * This enumeration is valid for bindings of type 1
 */
struct afb_verb_desc_v1
{
       const char *name;                       /* name of the verb */
       enum afb_session_v1 session;            /* authorisation and session requirements of the verb */
       void (*callback)(struct afb_req req);   /* callback function implementing the verb */
       const char *info;                       /* textual description of the verb */
};

/*
 * Description of the bindings of type 1
 */
struct afb_binding_desc_v1
{
       const char *info;                       /* textual information about the binding */
       const char *prefix;                     /* required prefix name for the binding */
       const struct afb_verb_desc_v1 *verbs;   /* array of descriptions of verbs terminated by a NULL name */
};

/*
 * Description of a binding
 */
struct afb_binding
{
       enum afb_binding_type type; /* type of the binding */
       union {
               struct afb_binding_desc_v1 v1;   /* description of the binding of type 1 */
       };
};

/*
 * config mode
 */
enum afb_mode {
       AFB_MODE_LOCAL = 0,     /* run locally */
       AFB_MODE_REMOTE,        /* run remotely */
       AFB_MODE_GLOBAL         /* run either remotely or locally (DONT USE! reserved for future) */
};

/* declaration of features of libsystemd */
struct sd_event;
struct sd_bus;

/*
 * Definition of the facilities provided by the daemon.
 */
struct afb_daemon_itf {
       int (*event_broadcast)(void *closure, const char *name, struct json_object *object); /* broadcasts evant 'name' with 'object' */
       struct sd_event *(*get_event_loop)(void *closure);      /* gets the common systemd's event loop */
       struct sd_bus *(*get_user_bus)(void *closure);          /* gets the common systemd's user d-bus */
       struct sd_bus *(*get_system_bus)(void *closure);        /* gets the common systemd's system d-bus */
       void (*vverbose)(void*closure, int level, const char *file, int line, const char *fmt, va_list args);
       struct afb_event (*event_make)(void *closure, const char *name); /* creates an event of 'name' */
};

/*
 * Structure for accessing daemon.
 * See also: afb_daemon_get_event_sender, afb_daemon_get_event_loop, afb_daemon_get_user_bus, afb_daemon_get_system_bus
 */
struct afb_daemon {
       const struct afb_daemon_itf *itf;       /* the interfacing functions */
       void *closure;                          /* the closure when calling these functions */
};

/*
 * Interface between the daemon and the binding.
 */
struct afb_binding_interface
{
       struct afb_daemon daemon;       /* access to the daemon facilies */
       int verbosity;                  /* level of verbosity */
       enum afb_mode mode;             /* run mode (local or remote) */
};

/*
 * Function for registering the binding
 */
extern const struct afb_binding *afbBindingV1Register (const struct afb_binding_interface *interface);

/*
 * Retrieves the common systemd's event loop of AFB
 * 'daemon' MUST be the daemon given in interface when activating the binding.
 */
static inline struct sd_event *afb_daemon_get_event_loop(struct afb_daemon daemon)
{
	return daemon.itf->get_event_loop(daemon.closure);
}

/*
 * Retrieves the common systemd's user/session d-bus of AFB
 * 'daemon' MUST be the daemon given in interface when activating the binding.
 */
static inline struct sd_bus *afb_daemon_get_user_bus(struct afb_daemon daemon)
{
	return daemon.itf->get_user_bus(daemon.closure);
}

/*
 * Retrieves the common systemd's system d-bus of AFB
 * 'daemon' MUST be the daemon given in interface when activating the binding.
 */
static inline struct sd_bus *afb_daemon_get_system_bus(struct afb_daemon daemon)
{
	return daemon.itf->get_system_bus(daemon.closure);
}

/*
 * Broadcasts widely the event of 'name' with the data 'object'.
 * 'object' can be NULL.
 * 'daemon' MUST be the daemon given in interface when activating the binding.
 *
 * For convenience, the function calls 'json_object_put' for 'object'.
 * Thus, in the case where 'object' should remain available after
 * the function returns, the function 'json_object_get' shall be used.
 *
 * Returns the count of clients that received the event.
 */
static inline int afb_daemon_broadcast_event(struct afb_daemon daemon, const char *name, struct json_object *object)
{
	return daemon.itf->event_broadcast(daemon.closure, name, object);
}

/*
 * Creates an event of 'name' and returns it.
 * 'daemon' MUST be the daemon given in interface when activating the binding.
 */
static inline struct afb_event afb_daemon_make_event(struct afb_daemon daemon, const char *name)
{
	return daemon.itf->event_make(daemon.closure, name);
}

/*
 * Send a message described by 'fmt' and following parameters
 * to the journal for the verbosity 'level'.
 * 'file' and 'line' are indicators of position of the code in source files.
 * 'daemon' MUST be the daemon given in interface when activating the binding.
 */
static inline void afb_daemon_verbose(struct afb_daemon daemon, int level, const char *file, int line, const char *fmt, ...) __attribute__((format(printf, 5, 6)));
static inline void afb_daemon_verbose(struct afb_daemon daemon, int level, const char *file, int line, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	daemon.itf->vverbose(daemon.closure, level, file, line, fmt, args);
	va_end(args);
}

/*
 * Macros for logging messages
 */
#if !defined(NO_BINDING_VERBOSE_MACRO)
# if !defined(NO_BINDING_FILE_LINE_INDICATION)
#  define ERROR(itf,...)   do{if(itf->verbosity>=0)afb_daemon_verbose(itf->daemon,3,__FILE__,__LINE__,__VA_ARGS__);}while(0)
#  define WARNING(itf,...) do{if(itf->verbosity>=1)afb_daemon_verbose(itf->daemon,4,__FILE__,__LINE__,__VA_ARGS__);}while(0)
#  define NOTICE(itf,...)  do{if(itf->verbosity>=1)afb_daemon_verbose(itf->daemon,5,__FILE__,__LINE__,__VA_ARGS__);}while(0)
#  define INFO(itf,...)    do{if(itf->verbosity>=2)afb_daemon_verbose(itf->daemon,6,__FILE__,__LINE__,__VA_ARGS__);}while(0)
#  define DEBUG(itf,...)   do{if(itf->verbosity>=3)afb_daemon_verbose(itf->daemon,7,__FILE__,__LINE__,__VA_ARGS__);}while(0)
# else
#  define ERROR(itf,...)   do{if(itf->verbosity>=0)afb_daemon_verbose(itf->daemon,3,NULL,0,__VA_ARGS__);}while(0)
#  define WARNING(itf,...) do{if(itf->verbosity>=1)afb_daemon_verbose(itf->daemon,4,NULL,0,__VA_ARGS__);}while(0)
#  define NOTICE(itf,...)  do{if(itf->verbosity>=1)afb_daemon_verbose(itf->daemon,5,NULL,0,__VA_ARGS__);}while(0)
#  define INFO(itf,...)    do{if(itf->verbosity>=2)afb_daemon_verbose(itf->daemon,6,NULL,0,__VA_ARGS__);}while(0)
#  define DEBUG(itf,...)   do{if(itf->verbosity>=3)afb_daemon_verbose(itf->daemon,7,NULL,0,__VA_ARGS__);}while(0)
# endif
#endif

