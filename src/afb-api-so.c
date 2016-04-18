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
#include "afb-pollmgr-itf.h"
#include "afb-evmgr-itf.h"

#include "session.h"
#include "afb-apis.h"
#include "afb-api-so.h"
#include "verbose.h"
#include "utils-upoll.h"

extern __thread sigjmp_buf *error_handler;

struct api_so_desc {
	struct AFB_plugin *plugin;	/* descriptor */
	void *handle;			/* context of dlopen */
	struct AFB_interface interface;	/* interface */
};

static int api_timeout = 15;

static const char plugin_register_function[] = "pluginRegister";

static const struct afb_pollmgr_itf pollmgr_itf = {
	.wait = (void*)upoll_wait,
	.open = (void*)upoll_open,
	.on_readable = (void*)upoll_on_readable,
	.on_writable = (void*)upoll_on_writable,
	.on_hangup = (void*)upoll_on_hangup,
	.close = (void*)upoll_close
};

static void afb_api_so_evmgr_push(struct api_so_desc *desc, const char *name, struct json_object *object)
{
}

static const struct afb_evmgr_itf evmgr_itf = {
	.push = (void*)afb_api_so_evmgr_push
};

static struct afb_evmgr afb_api_so_get_evmgr(struct api_so_desc *desc)
{
	return (struct afb_evmgr){ .itf = &evmgr_itf, .closure = desc };
}

static struct afb_pollmgr afb_api_so_get_pollmgr(struct api_so_desc *desc)
{
	return (struct afb_pollmgr){ .itf = &pollmgr_itf, .closure = NULL };
}

static const struct afb_daemon_itf daemon_itf = {
	.get_evmgr = (void*)afb_api_so_get_evmgr,
	.get_pollmgr = (void*)afb_api_so_get_pollmgr
};

static void free_context(struct api_so_desc *desc, void *context)
{
	void (*cb)(void*);

	cb = desc->plugin->freeCtxCB;
	if (cb)
		cb(context);
	else
		free(context);
}

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

static void call_check(struct afb_req req, const struct AFB_restapi *verb)
{
	switch(verb->session) {
	case AFB_SESSION_CREATE:
		if (!afb_req_session_create(req))
			return;
		break;
	case AFB_SESSION_RENEW:
		if (!afb_req_session_check(req, 1))
			return;
		break;
	case AFB_SESSION_CLOSE:
	case AFB_SESSION_CHECK:
		if (!afb_req_session_check(req, 0))
			return;
		break;
	case AFB_SESSION_NONE:
	default:
		break;
	}
	trapping_call(req, verb->callback);

	if (verb->session == AFB_SESSION_CLOSE)
		afb_req_session_close(req);
}

static void call(struct api_so_desc *desc, struct afb_req req, const char *verb, size_t lenverb)
{
	const struct AFB_restapi *v;

	v = desc->plugin->apis;
	while (v->name && (strncasecmp(v->name, verb, lenverb) || v->name[lenverb]))
		v++;
	if (v->name)
		call_check(req, v);
	else
		afb_req_fail_f(req, "unknown-verb", "verb %.*s unknown within api %s", (int)lenverb, verb, desc->plugin->prefix);
}

int afb_api_so_add_plugin(const char *path)
{
	struct api_so_desc *desc;
	struct AFB_plugin *(*pluginRegisterFct) (const struct AFB_interface *interface);

	desc = calloc(1, sizeof *desc);
	if (desc == NULL) {
		fprintf(stderr, "[%s] out of memory\n", path);
		goto error;
	}

	// This is a loadable library let's check if it's a plugin
	desc->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (desc->handle == NULL) {
		fprintf(stderr, "[%s] not loadable, continuing...\n", path);
		goto error2;
	}

	/* retrieves the register function */
	pluginRegisterFct = dlsym(desc->handle, plugin_register_function);
	if (!pluginRegisterFct) {
		fprintf(stderr, "[%s] not an AFB plugin, continuing...\n", path);
		goto error3;
	}
	if (verbosity)
		fprintf(stderr, "[%s] is a valid AFB plugin\n", path);

	/* init the interface */
	desc->interface.verbosity = 0;
	desc->interface.mode = AFB_MODE_LOCAL;
	desc->interface.daemon.itf = &daemon_itf;
	desc->interface.daemon.closure = desc;

	/* init the plugin */
	desc->plugin = pluginRegisterFct(&desc->interface);
	if (desc->plugin == NULL) {
		fprintf(stderr, "ERROR: plugin [%s] register function failed. continuing...\n", path);
		goto error3;
	}

	/* check the returned structure */
	if (desc->plugin->type != AFB_PLUGIN_JSON) {
		fprintf(stderr, "ERROR: plugin [%s] invalid type %d...\n", path, desc->plugin->type);
		goto error3;
	}
	if (desc->plugin->prefix == NULL || *desc->plugin->prefix == 0) {
		fprintf(stderr, "ERROR: plugin [%s] bad prefix...\n", path);
		goto error3;
	}
	if (desc->plugin->info == NULL || *desc->plugin->info == 0) {
		fprintf(stderr, "ERROR: plugin [%s] bad description...\n", path);
		goto error3;
	}
	if (desc->plugin->apis == NULL) {
		fprintf(stderr, "ERROR: plugin [%s] no APIs...\n", path);
		goto error3;
	}

	/* records the plugin */
	if (afb_apis_add(desc->plugin->prefix, (struct afb_api){
			.closure = desc,
			.call = (void*)call,
			.free_context = (void*)free_context}) < 0) {
		fprintf(stderr, "ERROR: plugin [%s] can't be registered...\n", path);
		goto error3;
	}

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
		fprintf(stderr, "ERROR in scanning plugin directory %s, %m\n", path);
		return -1;
	}
	if (verbosity)
		fprintf(stderr, "Scanning dir=[%s] for plugins\n", path);

	/* scan each entry */
	if (end)
		path[end++] = '/';
	for (;;) {
		readdir_r(dir, &ent, &result);
		if (result == NULL)
			break;

		len = strlen(ent.d_name);
		if (len + end >= PATH_MAX) {
			fprintf(stderr, "path too long for %s\n", ent.d_name);
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
		fprintf(stderr, "path too long %lu [%.99s...]\n", (unsigned long)length, path);
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
		fprintf(stderr, "Invalid plugin path [%s]: %m\n", path);
	else if (S_ISDIR(st.st_mode))
		rc = afb_api_so_add_directory(path);
	else
		rc = afb_api_so_add_plugin(path);
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

