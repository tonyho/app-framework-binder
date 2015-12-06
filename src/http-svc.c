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

// proto missing from GCC
char *strcasestr(const char *haystack, const char *needle);

static int rqtcount = 0;  // dummy request rqtcount to make each message be different
static int postcount = 0;
static int aipUrlLen=0;  // do not compute apiurl for each call
static int baseUrlLen=0; // do not compute baseurl for each call

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

STATIC int servFile (struct MHD_Connection *connection, AFB_session *session, const char *url, char *filepath, int fd) {
    const char *etagCache;
    char etagValue[15];
    struct MHD_Response *response;
    struct stat sbuf; 
    int ret;

    if (fstat (fd, &sbuf) != 0) {
        fprintf(stderr, "Fail to stat file: [%s] error:%s\n", filepath, strerror(errno));
        return (FAILED);
    }
    
    // if url is a directory let's add index.html and redirect client
    if (S_ISDIR (sbuf.st_mode)) {
        strncpy (filepath, url, sizeof (filepath));

        if (url [strlen (url) -1] != '/') strncat (filepath, "/", sizeof (filepath));
        strncat (filepath, "index.html", sizeof (filepath));
        close (fd);
        response = MHD_create_response_from_buffer (0,"", MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header (response,MHD_HTTP_HEADER_LOCATION, filepath);
        ret = MHD_queue_response (connection, MHD_HTTP_MOVED_PERMANENTLY, response);

    } else  if (! S_ISREG (sbuf.st_mode)) { // only standard file any other one including symbolic links are refused.

        fprintf (stderr, "Fail file: [%s] is not a regular file\n", filepath);
        const char *errorstr = "<html><body>Alsa-Json-Gateway Invalid file type</body></html>";
        response = MHD_create_response_from_buffer (strlen (errorstr),
                     (void *) errorstr,	 MHD_RESPMEM_PERSISTENT);
        ret = MHD_queue_response (connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);

    } else {
    
        // https://developers.google.com/web/fundamentals/performance/optimizing-content-efficiency/http-caching?hl=fr
        // ftp://ftp.heanet.ie/disk1/www.gnu.org/software/libmicrohttpd/doxygen/dc/d0c/microhttpd_8h.html

        // Check etag value and load file only when modification date changes
        etagCache = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_IF_NONE_MATCH);
        computeEtag(etagValue, sizeof (etagValue), &sbuf);

        if (etagCache != NULL && strcmp(etagValue, etagCache) == 0) {
            close(fd); // file did not change since last upload
            if (verbose) fprintf(stderr, "Not Modify: [%s]\n", filepath);
            response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
            MHD_add_response_header(response, MHD_HTTP_HEADER_CACHE_CONTROL, session->cacheTimeout); // default one hour cache
            MHD_add_response_header(response, MHD_HTTP_HEADER_ETAG, etagValue);
            ret = MHD_queue_response(connection, MHD_HTTP_NOT_MODIFIED, response);

        } else { // it's a new file, we need to upload it to client
            if (verbose) fprintf(stderr, "Serving: [%s]\n", filepath);
            response = MHD_create_response_from_fd(sbuf.st_size, fd);
            MHD_add_response_header(response, MHD_HTTP_HEADER_CACHE_CONTROL, session->cacheTimeout); // default one hour cache
            MHD_add_response_header(response, MHD_HTTP_HEADER_ETAG, etagValue);
            ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        }
    }
    MHD_destroy_response(response);
    return (ret);

}

// minimal httpd file server for static HTML,JS,CSS,etc...
STATIC int requestFile(struct MHD_Connection *connection, AFB_session *session, const char* url) {
    int fd;
    int ret;

    char filepath [512];

    // build full path from rootdir + url
    strncpy(filepath, session->config->rootdir, sizeof (filepath));
    strncat(filepath, url, 511);

    // try to open file and get its size
    if (-1 == (fd = open(filepath, O_RDONLY))) {
        fprintf(stderr, "Fail to open file: [%s] error:%s\n", filepath, strerror(errno));
        return (FAILED);

    }
    // open file is OK let use it
    ret = servFile (connection, session, url, filepath, fd);
    return ret;
}

// this function return either Index.htlm or a redirect to /#!route to make angular happy
STATIC int checkHTML5(struct MHD_Connection *connection, AFB_session *session, const char* url) {

    int fd;
    int ret;
    struct MHD_Response *response;
    char filepath [512];

    // if requesting '/' serve index.html
    if (strlen (url) == 0) {
        strncpy(filepath, session->config->rootdir, sizeof (filepath));
        strncat(filepath, "/index.html", sizeof (filepath));
        // try to open file and get its size
        if (-1 == (fd = open(filepath, O_RDONLY))) {
            fprintf(stderr, "Fail to open file: [%s] error:%s\n", filepath, strerror(errno));
            // Nothing respond to this request Files, API, Angular Base
            const char *errorstr = "<html><body>Alsa-Json-Gateway Unknown or Not readable file</body></html>";
            response = MHD_create_response_from_buffer(strlen(errorstr),(void *)errorstr, MHD_RESPMEM_PERSISTENT);
            ret = MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
            ret = MHD_YES;
            return (FAILED);
       } else {
            ret = servFile (connection, session, url, filepath, fd);
            return ret;
       }
    }

    // we are facing a internal route within the HTML5 OnePageApp let's redirect ex: /myapp/#!user/login
    strncpy(filepath, session->config->rootbase, sizeof (filepath));
    strncat(filepath, "#!", sizeof (filepath));
    strncat(filepath, url, sizeof (filepath));
    response = MHD_create_response_from_buffer(session->config->html5.len,(void *)session->config->html5.msg, MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header (response, "Location", "http://somesite.com/page.html");
    MHD_queue_response (connection, MHD_HTTP_OK, response);
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
    
    // this is an Angular request we change URL /!#xxxxx   
    if (0 == strncmp(url, session->config->rootapi, baseUrlLen)) {
        ret = doRestApi(connection, session, method, &url[baseUrlLen]);
        return ret;
    }
    
    // From here only accept get request
    if (0 != strcmp(method, MHD_HTTP_METHOD_GET)) return MHD_NO; /* unexpected method */
   
    // If a static file exist serve it now
    ret = requestFile(connection, session, url);
    if (ret != FAILED) return ret;
    
    // no static was served let check for Angular redirect
    if (0 == strncmp(url, session->config->rootbase, baseUrlLen)) {
        ret = checkHTML5(connection, session, &url[baseUrlLen]);
        return ret;
    }

     // Nothing respond to this request Files, API, Angular Base
    const char *errorstr = "<html><body>Alsa-Json-Gateway Unknown or Not readable file</body></html>";
    response = MHD_create_response_from_buffer(strlen(errorstr), (void*)errorstr, MHD_RESPMEM_PERSISTENT);
    ret = MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
    return (MHD_YES);
}

STATIC int newClient(void *cls, const struct sockaddr * addr, socklen_t addrlen) {
    // check if client is comming from an acceptable IP
    return (MHD_YES); // MHD_NO
}


PUBLIC AFB_ERROR httpdStart(AFB_session *session) {
  
    // do this only once
    aipUrlLen  = strlen (session->config->rootapi);
    baseUrlLen = strlen (session->config->rootbase);

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
PUBLIC AFB_ERROR httpdLoop(AFB_session *session) {
    static int  count = 0;

    if (verbose) fprintf(stderr, "AFB:notice entering httpd waiting loop\n");
    if (session->foreground) {

        while (TRUE) {
            fprintf(stderr, "AFB:notice Use Ctrl-C to quit");
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
