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


struct afb_req_itf;


struct afb_hreq_post {
	const char *upload_data;
	size_t *upload_data_size;
};

struct afb_hreq {
	AFB_session *session;
	struct MHD_Connection *connection;
	enum afb_method method;
	const char *version;
	const char *url;
	size_t lenurl;
	const char *tail;
	size_t lentail;
	struct afb_hreq **recorder;
	int (*post_handler) (struct afb_hreq *, struct afb_hreq_post *);
	int (*post_completed) (struct afb_hreq *, struct afb_hreq_post *);
	void *post_data;
};

extern int afb_hreq_unprefix(struct afb_hreq *request, const char *prefix, size_t length);

extern int afb_hreq_valid_tail(struct afb_hreq *request);

extern void afb_hreq_reply_error(struct afb_hreq *request, unsigned int status);

extern int afb_hreq_reply_file_if_exist(struct afb_hreq *request, int dirfd, const char *filename);

extern int afb_hreq_reply_file(struct afb_hreq *request, int dirfd, const char *filename);

extern int afb_hreq_redirect_to(struct afb_hreq *request, const char *url);

extern const char *afb_hreq_get_cookie(struct afb_hreq *hreq, const char *name);

extern const char *afb_hreq_get_argument(struct afb_hreq *hreq, const char *name);

extern const char *afb_hreq_get_header(struct afb_hreq *hreq, const char *name);

extern struct afb_req_itf afb_hreq_itf;
