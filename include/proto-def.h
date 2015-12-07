/*
   alsajson-gw -- provide a REST/HTTP interface to ALSA-Mixer

   Copyright (C) 2015, Fulup Ar Foll

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   $Id: $
*/

// Rest-api
PUBLIC json_object* pingSample (AFB_plugin *plugin, AFB_session *session, AFB_request *post);
PUBLIC const char* getQueryValue (AFB_request * request, char *name);
PUBLIC int doRestApi(struct MHD_Connection *connection, AFB_session *session, const char *method, const char* url);
void initPlugins (AFB_session *session);

typedef AFB_plugin* (*AFB_pluginCB)(AFB_session *session);
PUBLIC  AFB_plugin* afsvRegister (AFB_session *session);
PUBLIC  AFB_plugin* dbusRegister (AFB_session *session);
PUBLIC  AFB_plugin* alsaRegister (AFB_session *session);



// Session handling
PUBLIC AFB_error sessionCheckdir     (AFB_session *session);
PUBLIC json_object *sessionList      (AFB_session *session, AFB_request *request);
PUBLIC json_object *sessionToDisk    (AFB_session *session, AFB_request *request, char *name,json_object *jsonSession);
PUBLIC json_object *sessionFromDisk  (AFB_session *session, AFB_request *request, char *name);


// Httpd server
PUBLIC AFB_error httpdStart          (AFB_session *session);
PUBLIC AFB_error httpdLoop           (AFB_session *session);
PUBLIC void  httpdStop               (AFB_session *session);


// config management
PUBLIC char *configTime        (void);
PUBLIC AFB_session *configInit (void);
PUBLIC json_object *jsonNewMessage (AFB_error level, char* format, ...);
PUBLIC json_object *jsonNewStatus (AFB_error level);
PUBLIC json_object *jsonNewjtype (void);
PUBLIC json_object *jsonNewMessage (AFB_error level, char* format, ...);
PUBLIC void jsonDumpObject (json_object * jObject);
PUBLIC AFB_error configLoadFile (AFB_session * session, AFB_config *cliconfig);
PUBLIC void configStoreFile (AFB_session * session);


