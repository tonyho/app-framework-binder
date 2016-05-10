/*
 Copyright 2016 IoT.bzh

 author: José Bollo <jose.bollo@iot.bzh>

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

#pragma once

extern int verbosity;
extern void verbose(int level, const char *file, int line, const char *fmt, ...);

#define ERROR(...)   do{if(verbosity>=0)verbose(3,__FILE__,__LINE__,__VA_ARGS__);}while(0)
#define WARNING(...) do{if(verbosity>=1)verbose(4,__FILE__,__LINE__,__VA_ARGS__);}while(0)
#define NOTICE(...)  do{if(verbosity>=1)verbose(5,__FILE__,__LINE__,__VA_ARGS__);}while(0)
#define INFO(...)    do{if(verbosity>=2)verbose(6,__FILE__,__LINE__,__VA_ARGS__);}while(0)
#define DEBUG(...)   do{if(verbosity>=3)verbose(7,__FILE__,__LINE__,__VA_ARGS__);}while(0)

