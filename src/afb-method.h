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


enum afb_method {
	afb_method_none = 0,
	afb_method_get = 1,
	afb_method_post = 2,
	afb_method_head = 4,
	afb_method_connect = 8,
	afb_method_delete = 16,
	afb_method_options = 32,
	afb_method_patch = 64,
	afb_method_put = 128,
	afb_method_trace = 256,
	afb_method_all = 511
};

extern enum afb_method get_method(const char *method);
extern const char *get_method_name(enum afb_method method);

