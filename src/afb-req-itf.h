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


struct afb_req_itf {
	const char *(*argument)(void *data, const char *name);
	int (*is_argument_file)(void *data, const char *name);
	int (*iterate_arguments)(void *data, int (*iterator)(void *closure, const char *key, const char *value, int isfile), void *closure);
};

struct afb_req {
	const struct afb_req_itf *itf;
	void *data;
};

static inline const char *afb_req_argument(struct afb_req req, const char *name)
{
	return req.itf->argument(req.data, name);
}

static inline int afb_req_argument_file(struct afb_req req, const char *name)
{
	return req.itf->is_argument_file(req.data, name);
}

static inline int afb_req_iterate_arguments(struct afb_req req, int (*iterator)(void *closure, const char *key, const char *value, int isfile), void *closure)
{
	return req.itf->iterate_arguments(req.data, iterator, closure);
}




