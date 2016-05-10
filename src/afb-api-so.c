/*
 * Copyright (C) 2016 "IoT.bzh"
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

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <dirent.h>
#include <dlfcn.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <sys/syscall.h>
#include <setjmp.h>

#include "afb-plugin.h"
#include "afb-req-itf.h"
#include "afb-evmgr-itf.h"

#include "session.h"
#include "afb-common.h"
#include "afb-context.h"
#include "afb-apis.h"
#include "afb-api-so.h"
#include "verbose.h"

extern __thread sigjmp_buf *error_handler;

struct api_so_desc {
	struct AFB_plugin *plugin;	/* descriptor */
	size_t apilength;
	void *handle;			/* context of dlopen */
	struct AFB_interface interface;	/* interface */
};

static int api_timeout = 15;

static const char plugin_register_function[] = "pluginRegister";

static void afb_api_so_evmgr_push(struct api_so_desc *desc, const char *name, struct json_object *object)
{
	size_t length;
	char *event;

	assert(desc->plugin != NULL);
	length = strlen(name);
	event = alloca(length + 2 + desc->apilength);
	memcpy(event, desc->plugin->prefix, desc->apilength);
	event[desc->apilength] = '/';
	memcpy(event + desc->apilength + 1, name, length + 1);
	ctxClientEventSend(NULL, event, object);
}

static const struct afb_evmgr_itf evmgr_itf = {
	.push = (void*)afb_api_so_evmgr_push
};

static struct afb_evmgr afb_api_so_get_evmgr(struct api_so_desc *desc)
{
	return (struct afb_evmgr){ .itf = &evmgr_itf, .closure = desc };
}

static const struct afb_daemon_itf daemon_itf = {
	.get_evmgr = (void*)afb_api_so_get_evmgr,
	.get_event_loop = (void*)afb_common_get_event_loop,
	.get_user_bus = (void*)afb_common_get_user_bus,
	.get_system_bus = (void*)afb_common_get_system_bus
};


static void trapping_call(struct afb_req req, void(*cb)(struct afb_req))
{
	volatile int signum, timerset;
	timer_t timerid;
	sigjmp_buf jmpbuf, *older;
	struct sigevent sevp;
	struct itimerspec its;

	timerset = 0;
	older = error_handler;
	signum = setjmp(jmpbuf);
	if (signum != 0) {
		afb_req_fail_f(req, "aborted", "signal %d caught", signum);
	}
	else {
		error_handler = &jmpbuf;
		if (api_timeout > 0) {
			timerset = 1; /* TODO: check statuses */
			sevp.sigev_notify = SIGEV_THREAD_ID;
			sevp.sigev_signo = SIGALRM;
			sevp.sigev_value.sival_ptr = NULL;
#if defined(sigev_notify_thread_id)
			sevp.sigev_notify_thread_id = (pid_t)syscall(SYS_gettid);
#else
			sevp._sigev_un._tid = (pid_t)syscall(SYS_gettid);
#endif
			timer_create(CLOCK_THREAD_CPUTIME_ID, &sevp, &timerid);
			its.it_interval.tv_sec = 0;
			its.it_interval.tv_nsec = 0;
			its.it_value.tv_sec = api_timeout;
			its.it_value.tv_nsec = 0;
			timer_settime(timerid, 0, &its, NULL);
		}

		cb(req);
	}
	if (timerset)
		timer_delete(timerid);
	error_handler = older;
}

static void call_check(struct afb_req req, struct afb_context *context, const struct AFB_restapi *verb)
{
	int stag = (int)(verb->session & AFB_SESSION_MASK);

	if (stag != AFB_SESSION_NONE) {
		if (!afb_context_check(context)) {
			afb_context_close(context);
			afb_req_fail(req, "failed", "invalid token's identity");
			return;
		}	
	}

	if ((stag & AFB_SESSION_CREATE) != 0) {
		if (!afb_context_create(context)) {
			afb_context_close(context);
			afb_req_fail(req, "failed", "invalid creation state");
			return;
		}
	}
	
	if ((stag & (AFB_SESSION_CREATE | AFB_SESSION_RENEW)) != 0)
		afb_context_refresh(context);

	if ((stag & AFB_SESSION_CLOSE) != 0)
		afb_context_close(context);

	trapping_call(req, verb->callback);
}

static void call(struct api_so_desc *desc, struct afb_req req, struct afb_context *context, const char *verb, size_t lenverb)
{
	const struct AFB_restapi *v;

	v = desc->plugin->apis;
	while (v->name && (strncasecmp(v->name, verb, lenverb) || v->name[lenverb]))
		v++;
	if (v->name)
		call_check(req, context, v);
	else
		afb_req_fail_f(req, "unknown-verb", "verb %.*s unknown within api %s", (int)lenverb, verb, desc->plugin->prefix);
}

int afb_api_so_add_plugin(const char *path)
{
	struct api_so_desc *desc;
	struct AFB_plugin *(*pluginRegisterFct) (const struct AFB_interface *interface);

	desc = calloc(1, sizeof *desc);
	if (desc == NULL) {
		ERROR("out of memory");
		goto error;
	}

	// This is a loadable library let's check if it's a plugin
	desc->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (desc->handle == NULL) {
		ERROR("plugin [%s] not loadable", path);
		goto error2;
	}

	/* retrieves the register function */
	pluginRegisterFct = dlsym(desc->handle, plugin_register_function);
	if (!pluginRegisterFct) {
		ERROR("plugin [%s] is not an AFB plugin", path);
		goto error3;
	}
	INFO("plugin [%s] is a valid AFB plugin", path);

	/* init the interface */
	desc->interface.verbosity = 0;
	desc->interface.mode = AFB_MODE_LOCAL;
	desc->interface.daemon.itf = &daemon_itf;
	desc->interface.daemon.closure = desc;

	/* init the plugin */
	desc->plugin = pluginRegisterFct(&desc->interface);
	if (desc->plugin == NULL) {
		ERROR("plugin [%s] register function failed. continuing...", path);
		goto error3;
	}

	/* check the returned structure */
	if (desc->plugin->type != AFB_PLUGIN_JSON) {
		ERROR("plugin [%s] invalid type %d...", path, desc->plugin->type);
		goto error3;
	}
	if (desc->plugin->prefix == NULL || *desc->plugin->prefix == 0) {
		ERROR("plugin [%s] bad prefix...", path);
		goto error3;
	}
	if (desc->plugin->info == NULL || *desc->plugin->info == 0) {
		ERROR("plugin [%s] bad description...", path);
		goto error3;
	}
	if (desc->plugin->apis == NULL) {
		ERROR("plugin [%s] no APIs...", path);
		goto error3;
	}

	/* records the plugin */
	desc->apilength = strlen(desc->plugin->prefix);
	if (afb_apis_add(desc->plugin->prefix, (struct afb_api){
			.closure = desc,
			.call = (void*)call}) < 0) {
		ERROR("plugin [%s] can't be registered...", path);
		goto error3;
	}
	NOTICE("plugin %s loaded with API prefix %s", path, desc->plugin->prefix);
	return 0;

error3:
	dlclose(desc->handle);
error2:
	free(desc);
error:
	return -1;
}

static int adddirs(char path[PATH_MAX], size_t end)
{
	DIR *dir;
	struct dirent ent, *result;
	size_t len;

	/* open the DIR now */
	dir = opendir(path);
	if (dir == NULL) {
		ERROR("can't scan plugin directory %s, %m", path);
		return -1;
	}
	INFO("Scanning dir=[%s] for plugins", path);

	/* scan each entry */
	if (end)
		path[end++] = '/';
	for (;;) {
		readdir_r(dir, &ent, &result);
		if (result == NULL)
			break;

		len = strlen(ent.d_name);
		if (len + end >= PATH_MAX) {
			ERROR("path too long while scanning plugins for %s", ent.d_name);
			continue;
		}
		memcpy(&path[end], ent.d_name, len+1);
		if (ent.d_type == DT_DIR) {
			/* case of directories */
			if (ent.d_name[0] == '.') {
				if (len == 1)
					continue;
				if (ent.d_name[1] == '.' && len == 2)
					continue;
			}
			adddirs(path, end+len);;
		} else if (ent.d_type == DT_REG) {
			/* case of files */
			if (!strstr(ent.d_name, ".so"))
				continue;
			afb_api_so_add_plugin(path);
		}
	}
	closedir(dir);
	return 0;
}

int afb_api_so_add_directory(const char *path)
{
	size_t length;
	char buffer[PATH_MAX];

	length = strlen(path);
	if (length >= sizeof(buffer)) {
		ERROR("path too long %lu [%.99s...]", (unsigned long)length, path);
		return -1;
	}

	memcpy(buffer, path, length + 1);
	return adddirs(buffer, length);
}

int afb_api_so_add_path(const char *path)
{
	struct stat st;
	int rc;

	rc = stat(path, &st);
	if (rc < 0)
		ERROR("Invalid plugin path [%s]: %m", path);
	else if (S_ISDIR(st.st_mode))
		rc = afb_api_so_add_directory(path);
	else if (strstr(path, ".so"))
		rc = afb_api_so_add_plugin(path);
	else
		INFO("not a plugin [%s], skipped", path);
	return rc;
}

int afb_api_so_add_pathset(const char *pathset)
{
	static char sep[] = ":";
	char *ps, *p;

	ps = strdupa(pathset);
	for (;;) {
		p = strsep(&ps, sep);
		if (!p)
			return 0;
		afb_api_so_add_path(p);
	};
}

