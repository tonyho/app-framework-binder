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

struct json_object;

struct afb_arg {
	const char *name;
	const char *value;
	size_t size;
	int is_file;
};

struct afb_req_itf {
	struct afb_arg (*get)(void *data, const char *name);
	void (*iterate)(void *data, int (*iterator)(void *closure, struct afb_arg arg), void *closure);
	void (*fail)(void *data, const char *status, const char *info);
	void (*success)(void *data, struct json_object *obj, const char *info);
};

struct afb_req {
	const struct afb_req_itf *itf;
	void *data;
	void **context;
};

static inline struct afb_arg afb_req_get(struct afb_req req, const char *name)
{
	return req.itf->get(req.data, name);
}

static inline const char *afb_req_argument(struct afb_req req, const char *name)
{
	return afb_req_get(req, name).value;
}

static inline int afb_req_is_argument_file(struct afb_req req, const char *name)
{
	return afb_req_get(req, name).is_file;
}

static inline void afb_req_iterate(struct afb_req req, int (*iterator)(void *closure, struct afb_arg arg), void *closure)
{
	req.itf->iterate(req.data, iterator, closure);
}

static inline void afb_req_fail(struct afb_req req, const char *status, const char *info)
{
	req.itf->fail(req.data, status, info);
}

static inline void afb_req_success(struct afb_req req, struct json_object *obj, const char *info)
{
	req.itf->success(req.data, obj, info);
}

#if !defined(_GNU_SOURCE)
# error "_GNU_SOURCE must be defined for using vasprintf"
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

static inline void afb_req_fail_v(struct afb_req req, const char *status, const char *info, va_list args)
{
	char *message;
	if (info == NULL || vasprintf(&message, info, args) < 0)
		message = NULL;
	afb_req_fail(req, status, message);
	free(message);
}

static inline void afb_req_fail_f(struct afb_req req, const char *status, const char *info, ...)
{
	va_list args;
	va_start(args, info);
	afb_req_fail_v(req, status, info, args);
	va_end(args);
}

static inline void afb_req_success_v(struct afb_req req, struct json_object *obj, const char *info, va_list args)
{
	char *message;
	if (info == NULL || vasprintf(&message, info, args) < 0)
		message = NULL;
	afb_req_success(req, obj, message);
	free(message);
}

static inline void afb_req_success_f(struct afb_req req, struct json_object *obj, const char *info, ...)
{
	va_list args;
	va_start(args, info);
	afb_req_success_v(req, obj, info, args);
	va_end(args);
}

