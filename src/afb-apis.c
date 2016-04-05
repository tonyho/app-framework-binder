/*
 * Copyright (C) 2016 "IoT.bzh"
 * Author "Fulup Ar Foll"
 * Author José Bollo <jose.bollo@iot.bzh>
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
 * 
 * Contain all generic part to handle REST/API
 * 
 *  https://www.gnu.org/software/libmicrohttpd/tutorial.html [search 'largepost.c']
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

#include "local-def.h"

#include "afb-plugin.h"
#include "afb-req-itf.h"
#include "afb-poll-itf.h"

#include "session.h"
#include "afb-apis.h"
#include "verbose.h"
#include "utils-upoll.h"

struct api_desc {
	struct AFB_plugin *plugin;	/* descriptor */
	size_t prefixlen;
	const char *prefix;
	void *handle;		/* context of dlopen */
	struct AFB_interface *interface;
};

static int api_timeout = 15;
static struct api_desc *apis_array = NULL;
static int apis_count = 0;

static const char plugin_register_function[] = "pluginRegister";

static const struct afb_poll_itf upoll_itf = {
	.update = (void*)upoll_update,
	.close = (void*)upoll_close
};


int afb_apis_count()
{
	return apis_count;
}

void afb_apis_free_context(int apiidx, void *context)
{
	void (*cb)(void*);

	assert(0 <= apiidx && apiidx < apis_count);
	cb = apis_array[apiidx].plugin->freeCtxCB;
	if (cb)
		cb(context);
	else
		free(context);
}

static struct afb_poll itf_poll_open(int fd, uint32_t events, void (*process)(void *closure, int fd, uint32_t events), void *closure)
{
	struct afb_poll result;
	result.data = upoll_open(fd, events, process, closure);
	result.itf = result.data ? &upoll_itf : NULL;
	return result;
}


int afb_apis_add_plugin(const char *path)
{
	struct api_desc *apis;
	struct AFB_plugin *plugin;
	struct AFB_plugin *(*pluginRegisterFct) (const struct AFB_interface *interface);
	struct AFB_interface *interface;
	void *handle;
	int i;

	// This is a loadable library let's check if it's a plugin
	handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (handle == NULL) {
		fprintf(stderr, "[%s] not loadable, continuing...\n", path);
		goto error;
	}

	/* retrieves the register function */
	pluginRegisterFct = dlsym(handle, plugin_register_function);
	if (!pluginRegisterFct) {
		fprintf(stderr, "[%s] not an AFB plugin, continuing...\n", path);
		goto error2;
	}
	if (verbosity)
		fprintf(stderr, "[%s] is a valid AFB plugin\n", path);

	/* allocates enough memory */
	apis = realloc(apis_array, ((unsigned)apis_count + 1) * sizeof * apis);
	if (apis == NULL) {
		fprintf(stderr, "ERROR: plugin [%s] memory missing. continuing...\n", path);
		goto error2;
	}
	apis_array = apis;

	/* allocates the interface */
	interface = calloc(1, sizeof *interface);
	if (interface == NULL) {
		fprintf(stderr, "ERROR: plugin [%s] memory missing. continuing...\n", path);
		goto error2;
	}
	interface->verbosity = 0;
	interface->mode = AFB_MODE_LOCAL;
	interface->poll_open = itf_poll_open;

	/* init the plugin */
	plugin = pluginRegisterFct(interface);
	if (plugin == NULL) {
		fprintf(stderr, "ERROR: plugin [%s] register function failed. continuing...\n", path);
		goto error3;
	}

	/* check the returned structure */
	if (plugin->type != AFB_PLUGIN_JSON) {
		fprintf(stderr, "ERROR: plugin [%s] invalid type %d...\n", path, plugin->type);
		goto error3;
	}
	if (plugin->prefix == NULL || *plugin->prefix == 0) {
		fprintf(stderr, "ERROR: plugin [%s] bad prefix...\n", path);
		goto error3;
	}
	if (plugin->info == NULL || *plugin->info == 0) {
		fprintf(stderr, "ERROR: plugin [%s] bad description...\n", path);
		goto error3;
	}
	if (plugin->apis == NULL) {
		fprintf(stderr, "ERROR: plugin [%s] no APIs...\n", path);
		goto error3;
	}

	/* check previously existing plugin */
	for (i = 0 ; i < apis_count ; i++) {
		if (!strcasecmp(apis_array[i].prefix, plugin->prefix)) {
			fprintf(stderr, "ERROR: plugin [%s] prefix %s duplicated...\n", path, plugin->prefix);
			goto error2;
		}
	}

	/* record the plugin */
	if (verbosity)
		fprintf(stderr, "Loading plugin[%lu] prefix=[%s] info=%s\n", (unsigned long)apis_count, plugin->prefix, plugin->info);
	apis = &apis_array[apis_count];
	apis->plugin = plugin;
	apis->prefixlen = strlen(plugin->prefix);
	apis->prefix = plugin->prefix;
	apis->handle = handle;
	apis->interface = interface;
	apis_count++;

	return 0;

error3:
	free(interface);
error2:
	dlclose(handle);
error:
	return -1;
}

static int adddirs(char path[PATH_MAX], size_t end)
{
	int rc;
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
			rc = adddirs(path, end+len);;
		} else if (ent.d_type == DT_REG) {
			/* case of files */
			if (!strstr(ent.d_name, ".so"))
				continue;
			rc = afb_apis_add_plugin(path);
		}
	}
	closedir(dir);
	return 0;
}

int afb_apis_add_directory(const char *path)
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

int afb_apis_add_path(const char *path)
{
	struct stat st;
	int rc;

	rc = stat(path, &st);
	if (rc < 0)
		fprintf(stderr, "Invalid plugin path [%s]: %m\n", path);
	else if (S_ISDIR(st.st_mode))
		rc = afb_apis_add_directory(path);
	else
		rc = afb_apis_add_plugin(path);
	return rc;
}

int afb_apis_add_pathset(const char *pathset)
{
	static char sep[] = ":";
	char *ps, *p;
	int rc;

	ps = strdupa(pathset);
	for (;;) {
		p = strsep(&ps, sep);
		if (!p)
			return 0;
		rc = afb_apis_add_path(p);
	};
}

// Check of apiurl is declare in this plugin and call it
extern __thread sigjmp_buf *error_handler;
static void trapping_handle(struct afb_req req, void(*cb)(struct afb_req))
{
	volatile int signum, timerset;
	timer_t timerid;
	sigjmp_buf jmpbuf, *older;
	struct sigevent sevp;
	struct itimerspec its;

	// save context before calling the API
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

static void handle(struct afb_req req, const struct AFB_restapi *verb)
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
	trapping_handle(req, verb->callback);

	if (verb->session == AFB_SESSION_CLOSE)
		afb_req_session_close(req);
}

int afb_apis_handle(struct afb_req req, struct AFB_clientCtx *context, const char *api, size_t lenapi, const char *verb, size_t lenverb)
{
	int i, j;
	const struct api_desc *a;
	const struct AFB_restapi *v;

	a = apis_array;
	for (i = 0 ; i < apis_count ; i++, a++) {
		if (a->prefixlen == lenapi && !strncasecmp(a->prefix, api, lenapi)) {
			v = a->plugin->apis;
			for (j = 0 ; v->name ; j++, v++) {
				if (!strncasecmp(v->name, verb, lenverb) && !v->name[lenverb]) {
					req.context = context->contexts[i];
					handle(req, v);
					context->contexts[i] = req.context;
					return 1;
				}
			}
			afb_req_fail_f(req, "unknown-verb", "verb %.*s unknown within api %s", (int)lenverb, verb, a->prefix);
			return 1;
		}
	}
	return 0;
}

