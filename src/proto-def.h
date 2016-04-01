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
extern size_t getQueryAll(AFB_request * request, char *query, size_t len);
/*
extern json_object* getPostFile (AFB_request *request, AFB_PostItem *item, char* destination) ;
extern char* getPostPath (AFB_request *request);
*/

extern json_object *jsonNewMessage (AFB_error level, char* format, ...);


// Httpd server
extern AFB_error httpdStart          (AFB_session *session);
extern AFB_error httpdLoop           (AFB_session *session);
extern void  httpdStop               (AFB_session *session);




