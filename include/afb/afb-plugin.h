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

#include <afb/afb-req-itf.h>
#include <afb/afb-evmgr-itf.h>

/*
 * Definition of the versions of the plugin.
 * The definition uses hashes.
 */
enum  AFB_plugin_version
{
       AFB_PLUGIN_VERSION_1 = 123456789        /* version 1 */
};

/*
 * Enum for Session/Token/Authentication middleware.
 * This enumeration is valid for plugins of type 1
 */
enum AFB_session_v1
{
       AFB_SESSION_NONE = 0,   /* no session and no authentification required */
       AFB_SESSION_CREATE = 1, /* requires authentification and first call of the session */
       AFB_SESSION_CLOSE = 2,  /* closes the session after authentification */
       AFB_SESSION_RENEW = 4,  /* refreshes the token after authentification */
       AFB_SESSION_CHECK = 8,  /* enforce authentification */
       AFB_SESSION_MASK = 15   /* convenient mask used internally, bit-or of the previous masks */
};

/*
 * Description of one verb of the API provided by the plugin
 * This enumeration is valid for plugins of type 1
 */
struct AFB_verb_desc_v1
{
       const char *name;                       /* name of the verb */
       enum AFB_session_v1 session;            /* authorisation and session requirements of the verb */
       void (*callback)(struct afb_req req);   /* callback function implementing the verb */
       const char *info;                       /* textual description of the verb */
};

/*
 * Description of the plugins of type 1
 */
struct AFB_plugin_desc_v1
{
       const char *info;                       /* textual information about the plugin */
       const char *prefix;                     /* required prefix name for the plugin */
       const struct AFB_verb_desc_v1 *verbs;   /* array of descriptions of verbs terminated by a NULL name */
};

/*
 * Description of a plugin
 */
struct AFB_plugin
{
       enum AFB_plugin_version type; /* type of the plugin */
       union {
               struct AFB_plugin_desc_v1 v1;   /* description of the plugin of type 1 */
       };
};

/*
 * config mode
 */
enum AFB_Mode {
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
       struct afb_evmgr (*get_evmgr)(void *closure);           /* get the event manager */
       struct sd_event *(*get_event_loop)(void *closure);      /* get the common systemd's event loop */
       struct sd_bus *(*get_user_bus)(void *closure);          /* get the common systemd's user d-bus */
       struct sd_bus *(*get_system_bus)(void *closure);        /* get the common systemd's system d-bus */
};

/*
 * Structure for accessing daemon.
 * See also: afb_daemon_get_evmgr, afb_daemon_get_event_loop, afb_daemon_get_user_bus, afb_daemon_get_system_bus
 */
struct afb_daemon {
       const struct afb_daemon_itf *itf;       /* the interfacing functions */
       void *closure;                          /* the closure when calling these functions */
};

/*
 * Interface between the daemon and the plugin.
 */
struct AFB_interface
{
       struct afb_daemon daemon;       /* access to the daemon facilies */
       int verbosity;                  /* level of verbosity */
       enum AFB_Mode mode;             /* run mode (local or remote) */
};

/*
 * The function for registering the plugin to AFB
 */
extern const struct AFB_plugin *pluginAfbV1Register (const struct AFB_interface *interface);

/*
 * Retrieves the event sender of AFB
 * 'daemon' MUST be the daemon given in interface when activating the plugin.
 */
static inline struct afb_evmgr afb_daemon_get_evmgr(struct afb_daemon daemon)
{
	return daemon.itf->get_evmgr(daemon.closure);
}

/*
 * Retrieves the common systemd's event loop of AFB
 * 'daemon' MUST be the daemon given in interface when activating the plugin.
 */
static inline struct sd_event *afb_daemon_get_event_loop(struct afb_daemon daemon)
{
	return daemon.itf->get_event_loop(daemon.closure);
}

/*
 * Retrieves the common systemd's user/session d-bus of AFB
 * 'daemon' MUST be the daemon given in interface when activating the plugin.
 */
static inline struct sd_bus *afb_daemon_get_user_bus(struct afb_daemon daemon)
{
	return daemon.itf->get_user_bus(daemon.closure);
}

/*
 * Retrieves the common systemd's system d-bus of AFB
 * 'daemon' MUST be the daemon given in interface when activating the plugin.
 */
static inline struct sd_bus *afb_daemon_get_system_bus(struct afb_daemon daemon)
{
	return daemon.itf->get_system_bus(daemon.closure);
}


