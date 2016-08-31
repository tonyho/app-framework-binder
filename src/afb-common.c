/*
 * Copyright (C) 2015, 2016 "IoT.bzh"
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <systemd/sd-event.h>
#include <systemd/sd-bus.h>

#include "afb-common.h"
#include "locale-root.h"

static const char *default_locale = NULL;
static struct locale_root *rootdir = NULL;

/*
struct sd_event *afb_common_get_thread_event_loop()
{
	sd_event *result;
	int rc = sd_event_default(&result);
	if (rc != 0) {
		errno = -rc;
		result = NULL;
	}
	return result;
}
*/

static void *sdopen(void **p, int (*f)(void **))
{
	if (*p == NULL) {
		int rc = f(p);
		if (rc < 0) {
			errno = -rc;
			*p = NULL;
		}
	}
	return *p;
}

static struct sd_bus *sdbusopen(struct sd_bus **p, int (*f)(struct sd_bus **))
{
	if (*p == NULL) {
		int rc = f(p);
		if (rc < 0) {
			errno = -rc;
			*p = NULL;
		} else {
			rc = sd_bus_attach_event(*p, afb_common_get_event_loop(), 0);
			if (rc < 0) {
				sd_bus_unref(*p);
				errno = -rc;
				*p = NULL;
			}
		}
	}
	return *p;
}

struct sd_event *afb_common_get_event_loop()
{
	static struct sd_event *result = NULL;
	return sdopen((void*)&result, (void*)sd_event_new);
}

struct sd_bus *afb_common_get_user_bus()
{
	static struct sd_bus *result = NULL;
	return sdbusopen((void*)&result, (void*)sd_bus_open_user);
}

struct sd_bus *afb_common_get_system_bus()
{
	static struct sd_bus *result = NULL;
	return sdbusopen((void*)&result, (void*)sd_bus_open_system);
}

void afb_common_default_locale_set(const char *locale)
{
	default_locale = locale;
}

const char *afb_common_default_locale_get()
{
	return default_locale;
}

int afb_common_rootdir_set(const char *dirname)
{
	int dirfd, rc;
	struct locale_root *root;
	struct locale_search *search;

	rc = -1;
	dirfd = openat(AT_FDCWD, dirname, O_PATH|O_DIRECTORY);
	if (dirfd < 0) {
		/* TODO message */
	} else {
		root = locale_root_create(dirfd);
		if (root == NULL) {
			/* TODO message */
			close(dirfd);
		} else {
			rc = 0;
			if (default_locale != NULL) {
				search = locale_root_search(root, default_locale, 0);
				if (search == NULL) {
					/* TODO message */
				} else {
					locale_root_set_default_search(root, search);
					locale_search_unref(search);
				}
			}
			locale_root_unref(rootdir);
			rootdir = root;
		}
	}
	return rc;
}

int afb_common_rootdir_get_fd()
{
	return locale_root_get_dirfd(rootdir);
}

int afb_common_rootdir_open_locale(const char *filename, int flags, const char *locale)
{
	return locale_root_open(rootdir, filename, flags, locale);
}


