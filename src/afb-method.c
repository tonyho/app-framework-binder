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


#include <microhttpd.h>

#include "afb-method.h"

enum afb_method get_method(const char *method)
{
	switch (method[0] & ~' ') {
	case 'C':
		return afb_method_connect;
	case 'D':
		return afb_method_delete;
	case 'G':
		return afb_method_get;
	case 'H':
		return afb_method_head;
	case 'O':
		return afb_method_options;
	case 'P':
		switch (method[1] & ~' ') {
		case 'A':
			return afb_method_patch;
		case 'O':
			return afb_method_post;
		case 'U':
			return afb_method_put;
		}
		break;
	case 'T':
		return afb_method_trace;
	}
	return afb_method_none;
}

#if !defined(MHD_HTTP_METHOD_PATCH)
#define MHD_HTTP_METHOD_PATCH "PATCH"
#endif
const char *get_method_name(enum afb_method method)
{
	switch (method) {
	case afb_method_get:
		return MHD_HTTP_METHOD_GET;
	case afb_method_post:
		return MHD_HTTP_METHOD_POST;
	case afb_method_head:
		return MHD_HTTP_METHOD_HEAD;
	case afb_method_connect:
		return MHD_HTTP_METHOD_CONNECT;
	case afb_method_delete:
		return MHD_HTTP_METHOD_DELETE;
	case afb_method_options:
		return MHD_HTTP_METHOD_OPTIONS;
	case afb_method_patch:
		return MHD_HTTP_METHOD_PATCH;
	case afb_method_put:
		return MHD_HTTP_METHOD_PUT;
	case afb_method_trace:
		return MHD_HTTP_METHOD_TRACE;
	default:
		return NULL;
	}
}


