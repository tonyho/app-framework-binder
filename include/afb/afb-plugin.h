/*
 * Copyright (C) 2016 "IoT.bzh"
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

#if defined(NO_PLUGIN_VERBOSE_MACRO)
#  define NO_BINDING_VERBOSE_MACRO
#endif

#if defined(NO_PLUGIN_FILE_LINE_INDICATION)
#  define NO_BINDING_FILE_LINE_INDICATION
#endif

#include "afb-binding.h"

#define AFB_plugin_version         afb_binding_type
#define AFB_PLUGIN_VERSION_1       AFB_BINDING_VERSION_1
#define AFB_plugin_desc_v1         afb_binding_desc_v1
#define AFB_plugin                 afb_binding
#define AFB_interface              afb_binding_interface
#define pluginAfbV1Register        afbBindingV1Register


#define AFB_Mode                   afb_mode
#define AFB_session_v1             afb_session_v1
#define AFB_verb_desc_v1           afb_verb_desc_v1
