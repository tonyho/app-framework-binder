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

#if !defined(NDEBUG)
#include <syslog.h>
extern int verbosity;
#define LOGUSER(app) openlog(app,LOG_PERROR,LOG_USER)
#define LOGAUTH(app) openlog(app,LOG_PERROR,LOG_AUTH)
#define ERROR(...)   syslog(LOG_ERR,__VA_ARGS__)
#define WARNING(...) do{if(verbosity)syslog(LOG_WARNING,__VA_ARGS__);}while(0)
#define NOTICE(...)  do{if(verbosity)syslog(LOG_NOTICE,__VA_ARGS__);}while(0)
#define INFO(...)    do{if(verbosity>1)syslog(LOG_INFO,__VA_ARGS__);}while(0)
#define DEBUG(...)   do{if(verbosity>2)syslog(LOG_DEBUG,__VA_ARGS__);}while(0)
#else
#include <syslog.h>
#define LOGUSER(app) openlog(app,LOG_PERROR,LOG_USER)
#define LOGAUTH(app) openlog(app,LOG_PERROR,LOG_AUTH)
extern void verbose_error(const char *file, int line);
#define ERROR(...)   verbose_error(__FILE__,__LINE__)
#define WARNING(...) do{/*nothing*/}while(0)
#define NOTICE(...)  do{/*nothing*/}while(0)
#define INFO(...)    do{/*nothing*/}while(0)
#define DEBUG(...)   do{/*nothing*/}while(0)
#endif
