/*
 Copyright (C) 2016 "IoT.bzh"

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

#if !defined(VERBOSE_WITH_SYSLOG)

  extern int verbosity;
  extern void verbose(int level, const char *file, int line, const char *fmt, ...);

# define ERROR(...)   do{if(verbosity>=0)verbose(3,__FILE__,__LINE__,__VA_ARGS__);}while(0)
# define WARNING(...) do{if(verbosity>=1)verbose(4,__FILE__,__LINE__,__VA_ARGS__);}while(0)
# define NOTICE(...)  do{if(verbosity>=1)verbose(5,__FILE__,__LINE__,__VA_ARGS__);}while(0)
# define INFO(...)    do{if(verbosity>=2)verbose(6,__FILE__,__LINE__,__VA_ARGS__);}while(0)
# define DEBUG(...)   do{if(verbosity>=3)verbose(7,__FILE__,__LINE__,__VA_ARGS__);}while(0)
# define LOGUSER(app) NOTICE("Starting user application %s",app)
# define LOGAUTH(app) NOTICE("Starting auth application %s",app)

#else /* VERBOSE_WITH_SYSLOG is defined */

# include <syslog.h>

# define LOGUSER(app) openlog(app,LOG_PERROR,LOG_USER)
# define LOGAUTH(app) openlog(app,LOG_PERROR,LOG_AUTH)

# if !defined(NDEBUG)

    extern int verbosity;
#   define ERROR(...)   syslog(LOG_ERR,__VA_ARGS__)
#   define WARNING(...) do{if(verbosity)syslog(LOG_WARNING,__VA_ARGS__);}while(0)
#   define NOTICE(...)  do{if(verbosity)syslog(LOG_NOTICE,__VA_ARGS__);}while(0)
#   define INFO(...)    do{if(verbosity>1)syslog(LOG_INFO,__VA_ARGS__);}while(0)
#   define DEBUG(...)   do{if(verbosity>2)syslog(LOG_DEBUG,__VA_ARGS__);}while(0)

# else

    extern void verbose_error(const char *file, int line);
#   define ERROR(...)   verbose_error(__FILE__,__LINE__)
#   define WARNING(...) do{/*nothing*/}while(0)
#   define NOTICE(...)  do{/*nothing*/}while(0)
#   define INFO(...)    do{/*nothing*/}while(0)
#   define DEBUG(...)   do{/*nothing*/}while(0)

# endif

#endif

