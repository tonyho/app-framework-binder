/*
 * Copyright 2016 IoT.bzh
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

#include "afb-req-itf.h"
#include "afb-evmgr-itf.h"

/* Plugin Type */
enum  AFB_pluginE
{
	AFB_PLUGIN_JSON = 123456789,
/*	AFB_PLUGIN_JSCRIPT = 987654321, */
	AFB_PLUGIN_RAW = 987123546
};

/* Enum for Session/Token/Authentication middleware */
enum AFB_sessionE
{
	AFB_SESSION_NONE,
	AFB_SESSION_CREATE,
	AFB_SESSION_CLOSE,
	AFB_SESSION_RENEW,
	AFB_SESSION_CHECK
};

/* API definition */
struct AFB_restapi
{
	const char *name;
	enum AFB_sessionE session;
	void (*callback)(struct afb_req req);
	const char *info;
};

/* Plugin definition */
struct AFB_plugin
{
	enum AFB_pluginE type;  
	const char *info;
	const char *prefix;
	const struct AFB_restapi *apis;
};

/* config mode */
enum AFB_Mode {
	AFB_MODE_LOCAL = 0,
	AFB_MODE_REMOTE,
	AFB_MODE_GLOBAL
};

struct sd_event;
struct sd_bus;

struct afb_daemon_itf {
	struct afb_evmgr (*get_evmgr)(void *closure);
	struct sd_event *(*get_event_loop)(void *closure);
	struct sd_bus *(*get_user_bus)(void *closure);
	struct sd_bus *(*get_system_bus)(void *closure);
};

struct afb_daemon {
	const struct afb_daemon_itf *itf;
	void *closure;
};

struct AFB_interface
{
	int verbosity;
	enum AFB_Mode mode;
	struct afb_daemon daemon;
};

extern const struct AFB_plugin *pluginRegister (const struct AFB_interface *interface);

static inline struct afb_evmgr afb_daemon_get_evmgr(struct afb_daemon daemon)
{
	return daemon.itf->get_evmgr(daemon.closure);
}

static inline struct sd_event *afb_daemon_get_event_loop(struct afb_daemon daemon)
{
	return daemon.itf->get_event_loop(daemon.closure);
}

static inline struct sd_bus *afb_daemon_get_user_bus(struct afb_daemon daemon)
{
	return daemon.itf->get_user_bus(daemon.closure);
}

static inline struct sd_bus *afb_daemon_get_system_bus(struct afb_daemon daemon)
{
	return daemon.itf->get_system_bus(daemon.closure);
}


