/*
   proto-def.h -- provide a REST/HTTP interface

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

*/

// helper-api
extern const char* getQueryValue (const AFB_request * request, const char *name);
extern int getQueryAll(AFB_request * request, char *query, size_t len);
extern AFB_PostHandle* getPostHandle (AFB_request *request);
extern json_object* getPostFile (AFB_request *request, AFB_PostItem *item, char* destination) ;
extern AFB_PostCtx* getPostContext (AFB_request *request);
extern char* getPostPath (AFB_request *request);

extern json_object *jsonNewMessage (AFB_error level, char* format, ...);
extern json_object *jsonNewStatus (AFB_error level);
extern json_object *jsonNewjtype (void);
extern json_object *jsonNewMessage (AFB_error level, char* format, ...);
extern void jsonDumpObject (json_object * jObject);

// rest-api
extern void endPostRequest(AFB_PostHandle *posthandle); 
extern int doRestApi(struct MHD_Connection *connection, AFB_session *session, const char* url, const char *method
    , const char *upload_data, size_t *upload_data_size, void **con_cls);

// Session handling
#if defined(ALLOWS_SESSION_FILES)
extern AFB_error sessionCheckdir     (AFB_session *session);
extern json_object *sessionList      (AFB_session *session, AFB_request *request);
extern json_object *sessionToDisk    (AFB_session *session, AFB_request *request, char *name,json_object *jsonSession);
extern json_object *sessionFromDisk  (AFB_session *session, AFB_request *request, char *name);
#endif

extern AFB_error ctxTokenRefresh (AFB_clientCtx *clientCtx, AFB_request *request);
extern AFB_error ctxTokenCreate (AFB_clientCtx *clientCtx, AFB_request *request);
extern AFB_error ctxTokenCheck (AFB_clientCtx *clientCtx, AFB_request *request);
extern AFB_error ctxTokenReset (AFB_clientCtx *clientCtx, AFB_request *request);
extern AFB_clientCtx *ctxClientGet (AFB_request *request, int idx);
extern void      ctxStoreInit (int);



// Httpd server
extern AFB_error httpdStart          (AFB_session *session);
extern AFB_error httpdLoop           (AFB_session *session);
extern void  httpdStop               (AFB_session *session);




