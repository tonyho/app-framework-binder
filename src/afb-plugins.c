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
#include <string.h>
#include <dirent.h>
#include <dlfcn.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "../include/local-def.h"

#include "afb-plugins.h"

static const char plugin_register_function[] = "pluginRegister";

AFB_plugin *afb_plugins_search(AFB_session *session, const char *prefix, size_t length)
{
	int i, n;
	AFB_plugin **plugins, *p;

	if (!length)
		length = strlen(prefix);

	n = session->config->pluginCount;
	plugins = session->plugins;

	for (i = 0 ; i < n ; i++) {
		p = plugins[i];
		if (p->prefixlen == length && !strcmp(p->prefix, prefix))
			return p;
	}
	return NULL;
}

int afb_plugins_add_plugin(AFB_session *session, const char *path)
{
	AFB_plugin *desc, *check, **plugins;
	AFB_plugin *(*pluginRegisterFct) (void);
	void *handle;
	size_t len;

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
	if (verbose)
		fprintf(stderr, "[%s] is a valid AFB plugin\n", path);

	/* allocates enough memory */
	plugins = realloc(session->plugins, ((unsigned)session->config->pluginCount + 2) * sizeof(AFB_plugin*));
	if (plugins == NULL) {
		fprintf(stderr, "ERROR: plugin [%s] memory missing. continuing...\n", path);
		goto error2;
	}
	session->plugins = plugins;

	/* init the plugin */
	desc = pluginRegisterFct();
	if (desc == NULL) {
		fprintf(stderr, "ERROR: plugin [%s] register function failed. continuing...\n", path);
		goto error2;
	}

	/* check the returned structure */
	if (desc->type != AFB_PLUGIN_JSON) {
		fprintf(stderr, "ERROR: plugin [%s] invalid type %d...\n", path, desc->type);
		goto error2;
	}
	if (desc->prefix == NULL || *desc->prefix == 0) {
		fprintf(stderr, "ERROR: plugin [%s] bad prefix...\n", path);
		goto error2;
	}
	if (desc->info == NULL || *desc->info == 0) {
		fprintf(stderr, "ERROR: plugin [%s] bad description...\n", path);
		goto error2;
	}
	if (desc->apis == NULL) {
		fprintf(stderr, "ERROR: plugin [%s] no APIs...\n", path);
		goto error2;
	}

	/* check previously existing plugin */
	len = strlen(desc->prefix);
	check = afb_plugins_search(session, desc->prefix, len);
	if (check != NULL) {
		fprintf(stderr, "ERROR: plugin [%s] prefix %s duplicated...\n", path, desc->prefix);
		goto error2;
	}

	/* Prebuild plugin jtype to boost API response */
	desc->jtype = json_object_new_string(desc->prefix);
	desc->prefixlen = len;

	/* record the plugin */
	session->plugins[session->config->pluginCount] = desc;
	session->plugins[++session->config->pluginCount] = NULL;

	if (verbose)
		fprintf(stderr, "Loading plugin[%d] prefix=[%s] info=%s\n", session->config->pluginCount, desc->prefix, desc->info);

	return 0;

error2:
	dlclose(handle);
error:
	return -1;
}

static int adddirs(AFB_session * session, char path[PATH_MAX], size_t end)
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
	if (verbose)
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
			rc = adddirs(session, path, end+len);;
		} else if (ent.d_type == DT_REG) {
			/* case of files */
			if (!strstr(ent.d_name, ".so"))
				continue;
			rc = afb_plugins_add_plugin(session, path);
		}
	}
	closedir(dir);
	return 0;
}

int afb_plugins_add_directory(AFB_session * session, const char *path)
{
	size_t length;
	char buffer[PATH_MAX];

	length = strlen(path);
	if (length >= sizeof(buffer)) {
		fprintf(stderr, "path too long %lu [%.99s...]\n", (unsigned long)length, path);
		return -1;
	}

	memcpy(buffer, path, length + 1);
	return adddirs(session, buffer, length);
}

int afb_plugins_add_path(AFB_session * session, const char *path)
{
	struct stat st;
	int rc;

	rc = stat(path, &st);
	if (rc < 0)
		fprintf(stderr, "Invalid plugin path [%s]: %m\n", path);
	else if (S_ISDIR(st.st_mode))
		rc = afb_plugins_add_directory(session, path);
	else
		rc = afb_plugins_add_plugin(session, path);
	return rc;
}

int afb_plugins_add_pathset(AFB_session * session, const char *pathset)
{
	static char sep[] = ":";
	char *ps, *p;
	int rc;

	ps = strdupa(pathset);
	for (;;) {
		p = strsep(&ps, sep);
		if (!p)
			return 0;
		rc = afb_plugins_add_path(session, p);
	};
}

void initPlugins(AFB_session * session)
{
	int rc = afb_plugins_add_pathset(session, session->config->ldpaths);
}

