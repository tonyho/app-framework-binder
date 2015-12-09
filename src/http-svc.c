/*
 * Copyright (C) 2015 "IoT.bzh"
 * Author "Fulup Ar Foll"
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * Handle standard HTTP request
 *    Features/Restriction:
    - handle ETAG to limit upload to modified/new files [cache default 3600s]
    - handles redirect to index.htlm when path is a directory [code 301]
    - only support GET method
    - does not follow link.

   References: https://www.gnu.org/software/libmicrohttpd/manual/html_node/index.html#Top
   http://libmicrohttpd.sourcearchive.com/documentation/0.4.2/microhttpd_8h.html
   https://gnunet.org/svn/libmicrohttpd/src/examples/fileserver_example_external_select.c
   https://github.com/json-c/json-c
   POST https://www.gnu.org/software/libmicrohttpd/manual/html_node/microhttpd_002dpost.html#microhttpd_002dpost
 */


#include <microhttpd.h>

#include <sys/stat.h>
#include "../include/local-def.h"

// let's compute fixed URL length only once
static apiUrlLen=0;
static baseUrlLen=0;
static rootUrlLen=0;

// proto missing from GCC
char *strcasestr(const char *haystack, const char *needle);

static int rqtcount = 0;  // dummy request rqtcount to make each message be different
static int postcount = 0;

// try to open libmagic to handle mime types
static AFB_error initLibMagic (AFB_session *session) {
  
    /*MAGIC_MIME tells magic to return a mime of the file, but you can specify different things*/
    if (verbose) printf("Loading mimetype default magic database\n");
  
    session->magic = magic_open(MAGIC_MIME_TYPE);
    if (session->magic == NULL) {
        fprintf(stderr,"ERROR: unable to initialize magic library\n");
        return AFB_FAIL;
    }
    
    // Warning: should not use NULL for DB [libmagic bug wont pass efence check]
    if (magic_load(session->magic, MAGIC_DB) != 0) {
        fprintf(stderr,"cannot load magic database - %s\n", magic_error(session->magic));
        magic_close(session->magic);
        return AFB_FAIL;
    }

    return AFB_SUCCESS;
}

// Because of POST call multiple time requestApi we need to free POST handle here
static void endRequest (void *cls, struct MHD_Connection *connection, void **con_cls, enum MHD_RequestTerminationCode toe) {
  AFB_HttpPost *posthandle = *con_cls;

  // if post handle was used let's free everything
  if (posthandle) {
     if (verbose) fprintf (stderr, "End Post Request UID=%d\n", posthandle->uid);
     free (posthandle->data);
     free (posthandle);
  }
}


// Create check etag value
STATIC void computeEtag(char *etag, int maxlen, struct stat *sbuf) {
    int time;
    time = sbuf->st_mtim.tv_sec;
    snprintf(etag, maxlen, "%d", time);
}

STATIC int servFile (struct MHD_Connection *connection, AFB_session *session, const char *url, AFB_staticfile *staticfile) {
    const char *etagCache, *mimetype; 
    char etagValue[15];
    struct MHD_Response *response;
    struct stat sbuf; 
    int ret;

    if (fstat (staticfile->fd, &sbuf) != 0) {
        fprintf(stderr, "Fail to stat file: [%s] error:%s\n", staticfile->path, strerror(errno));
        goto abortRequest;
    }
    
    if (! S_ISREG (sbuf.st_mode)) { // only standard file any other one including symbolic links are refused.
        close (staticfile->fd); // nothing useful to do with this file
        fprintf (stderr, "Fail file: [%s] is not a regular file\n", staticfile->path);
        const char *errorstr = "<html><body>Alsa-Json-Gateway Invalid file type</body></html>";
        response = MHD_create_response_from_buffer (strlen (errorstr),
                     (void *) errorstr,	 MHD_RESPMEM_PERSISTENT);
        MHD_queue_response (connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
        goto sendRequest;
    } 
    
    // if url is a directory let's add index.html and redirect client
    if (S_ISDIR (sbuf.st_mode)) {
        close (staticfile->fd); // close directory check for Index
       
        // No trailing '/'. Let's add one and redirect for relative paths to work
        if (url [strlen (url) -1] != '/') {
            response = MHD_create_response_from_buffer(0,"", MHD_RESPMEM_PERSISTENT);
            strncat(staticfile->path, "/", sizeof (staticfile->path));
            MHD_add_response_header (response, "Location", staticfile->path);
            MHD_queue_response (connection, MHD_HTTP_MOVED_PERMANENTLY, response);
            if (verbose) fprintf (stderr,"Adding trailing '/' [%s]\n",staticfile->path);      
            goto sendRequest;
        }
        
        strncat (staticfile->path, OPA_INDEX, sizeof (staticfile->path));
        if (-1 == (staticfile->fd = open(staticfile->path, O_RDONLY)) || (fstat (staticfile->fd, &sbuf) != 0)) {
           fprintf(stderr, "No Index.html in direcory [%s]\n", staticfile->path);
           goto abortRequest;  
        }      
    }   
    
    // https://developers.google.com/web/fundamentals/performance/optimizing-content-efficiency/http-caching?hl=fr
    // ftp://ftp.heanet.ie/disk1/www.gnu.org/software/libmicrohttpd/doxygen/dc/d0c/microhttpd_8h.html

    // Check etag value and load file only when modification date changes
    etagCache = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_IF_NONE_MATCH);
    computeEtag(etagValue, sizeof (etagValue), &sbuf);

    if (etagCache != NULL && strcmp(etagValue, etagCache) == 0) {
        close(staticfile->fd); // file did not change since last upload
        if (verbose) fprintf(stderr, "Not Modify: [%s]\n", staticfile->path);
        response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, MHD_HTTP_HEADER_CACHE_CONTROL, session->cacheTimeout); // default one hour cache
        MHD_add_response_header(response, MHD_HTTP_HEADER_ETAG, etagValue);
        MHD_queue_response(connection, MHD_HTTP_NOT_MODIFIED, response);

    } else { // it's a new file, we need to upload it to client
        // if we have magic let's try to guest mime type
        if (session->magic) {          
           mimetype= magic_descriptor(session->magic, staticfile->fd);
           if (mimetype != NULL)  MHD_add_response_header (response, MHD_HTTP_HEADER_CONTENT_TYPE, mimetype);
        } else mimetype="Unknown";
        
        if (verbose) fprintf(stderr, "Serving: [%s] mime=%s\n", staticfile->path, mimetype);
        response = MHD_create_response_from_fd(sbuf.st_size, staticfile->fd);
        MHD_add_response_header(response, MHD_HTTP_HEADER_CACHE_CONTROL, session->cacheTimeout); // default one hour cache
        MHD_add_response_header(response, MHD_HTTP_HEADER_ETAG, etagValue);
        MHD_queue_response(connection, MHD_HTTP_OK, response);
    }
    
sendRequest:    
    MHD_destroy_response(response);
    return (MHD_YES);

abortRequest:
    return (FAILED);
}


// this function return either Index.htlm or a redirect to /#!route to make angular happy
STATIC int redirectHTML5(struct MHD_Connection *connection, AFB_session *session, const char* url) {

    int fd;
    int ret;
    struct MHD_Response *response;
    AFB_staticfile staticfile;

    // Url match /opa/xxxx should redirect to "/opa/#!page" to force index.html reload
    strncpy(staticfile.path, session->config->rootbase, sizeof (staticfile.path));
    strncat(staticfile.path, "/#!", sizeof (staticfile.path));
    strncat(staticfile.path, &url[1], sizeof (staticfile.path));
    response = MHD_create_response_from_buffer(0,"", MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header (response, "Location", staticfile.path);
    MHD_queue_response (connection, MHD_HTTP_MOVED_PERMANENTLY, response);
    if (verbose) fprintf (stderr,"checkHTML5 redirect to [%s]\n",staticfile.path);
    return (MHD_YES);
}


// minimal httpd file server for static HTML,JS,CSS,etc...
STATIC int requestFile(struct MHD_Connection *connection, AFB_session *session, const char* url) {
    int fd, ret, idx;
    AFB_staticfile staticfile;
    char *requestdir, *requesturl;
   
    // default search for file is rootdir base
    requestdir = session->config->rootdir;
    requesturl=url;
    
    // Check for optional aliases
    for (idx=0; session->config->aliasdir[idx].url != NULL; idx++) {
        if (0 == strncmp(url, session->config->aliasdir[idx].url, session->config->aliasdir[idx].len)) {
             requestdir = session->config->aliasdir[idx].path;
             requesturl=&url[session->config->aliasdir[idx].len];
             break;
        }
    }
    
    // build full path from rootdir + url
    strncpy(staticfile.path, requestdir, sizeof (staticfile.path));   
    strncat(staticfile.path, requesturl, sizeof (staticfile.path));

    // try to open file and get its size
    if (-1 == (staticfile.fd = open(staticfile.path, O_RDONLY))) {
        fprintf(stderr, "Fail to open file: [%s] error:%s\n", staticfile.path, strerror(errno));
        return (FAILED);
    }
    // open file is OK let use it
    ret = servFile (connection, session, url, &staticfile);
    return ret;
}

// Check and Dispatch HTTP request
STATIC int newRequest(void *cls,
        struct MHD_Connection *connection,
        const char *url,
        const char *method,
        const char *version,
        const char *upload_data, size_t *upload_data_size, void **con_cls) {

    AFB_session *session = cls;
    struct MHD_Response *response;
    int ret;
    
    // this is a REST API let's check for plugins
    if (0 == strncmp(url, session->config->rootapi, apiUrlLen)) {
        ret = doRestApi(connection, session, &url[apiUrlLen+1], method, upload_data, upload_data_size, con_cls);
        return ret;
    }
    
    // From here only accept get request
    if (0 != strcmp(method, MHD_HTTP_METHOD_GET)) return MHD_NO; /* unexpected method */
   
    // If a static file exist serve it now
    ret = requestFile(connection, session, url);
    if (ret != FAILED) return ret;
    
    // no static was served let's try HTML5 OPA redirect
    if (0 == strncmp(url, session->config->rootbase, baseUrlLen)) {
        ret = redirectHTML5(connection, session, &url[baseUrlLen]);
        return ret;
    }

     // Nothing respond to this request Files, API, Angular Base
    const char *errorstr = "<html><body>Alsa-Json-Gateway Unknown or Not readable file</body></html>";
    response = MHD_create_response_from_buffer(strlen(errorstr), (void*)errorstr, MHD_RESPMEM_PERSISTENT);
    ret = MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
    return (MHD_YES);
}

STATIC int newClient(void *cls, const struct sockaddr * addr, socklen_t addrlen) {
    // check if client is coming from an acceptable IP
    return (MHD_YES); // MHD_NO
}


PUBLIC AFB_error httpdStart(AFB_session *session) {
    
    // compute fixed URL length at startup time
    apiUrlLen = strlen (session->config->rootapi);
    baseUrlLen= strlen (session->config->rootbase);
    rootUrlLen= strlen (session->config->rootdir);
     
    // TBD open libmagic cache [fail to pass EFENCE check]
    // initLibMagic (session);
    
    
    if (verbose) {
        printf("AFB:notice Waiting port=%d rootdir=%s\n", session->config->httpdPort, session->config->rootdir);
        printf("AFB:notice Browser URL= http://localhost:%d\n", session->config->httpdPort);
    }

    session->httpd = (void*) MHD_start_daemon(
            MHD_USE_SELECT_INTERNALLY | MHD_USE_DEBUG, // use request and not threads
            session->config->httpdPort, // port
            &newClient, NULL, // Tcp Accept call back + extra attribute
            &newRequest, session, // Http Request Call back + extra attribute
            MHD_OPTION_NOTIFY_COMPLETED, &endRequest, NULL,
            MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int) 15, MHD_OPTION_END); // 15s + options-end
    // TBD: MHD_OPTION_SOCK_ADDR

    if (session->httpd == NULL) {
        printf("Error: httpStart invalid httpd port: %d", session->config->httpdPort);
        return AFB_FATAL;
    }
    return AFB_SUCCESS;
}

// infinite loop
PUBLIC AFB_error httpdLoop(AFB_session *session) {
    static int  count = 0;

    if (verbose) fprintf(stderr, "AFB:notice entering httpd waiting loop\n");
    if (session->foreground) {

        while (TRUE) {
            fprintf(stderr, "AFB:notice Use Ctrl-C to quit\n");
            (void) getc(stdin);
        }
    } else {
        while (TRUE) {
            sleep(3600);
            if (verbose) fprintf(stderr, "AFB:notice httpd alive [%d]\n", count++);
        }
    }

    // should never return from here
    return AFB_FATAL;
}

PUBLIC int httpdStatus(AFB_session *session) {
    return (MHD_run(session->httpd));
}

PUBLIC void httpdStop(AFB_session *session) {
    MHD_stop_daemon(session->httpd);
}
