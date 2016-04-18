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

#pragma once

struct afb_pollmgr_itf
{
	int (*wait)(int timeout, void *pollclosure);
	void *(*open)(int fd, void *closure, void *pollclosure);
	int (*on_readable)(void *hndl, void (*cb)(void *closure));
	int (*on_writable)(void *hndl, void (*cb)(void *closure));
	void (*on_hangup)(void *hndl, void (*cb)(void *closure));
	void (*close)(void *hndl);
};

struct afb_pollmgr
{
	const struct afb_pollmgr_itf *itf;
	void *closure;
};

static inline int afb_pollmgr_wait(struct afb_pollmgr pollmgr, int timeout)
{
	return pollmgr.itf->wait(timeout, pollmgr.closure);
}

static inline void *afb_pollmgr_open(struct afb_pollmgr pollmgr, int fd, void *closure)
{
	return pollmgr.itf->open(fd, closure, pollmgr.closure);
}

static inline int afb_pollmgr_on_readable(struct afb_pollmgr pollmgr, void *hndl, void (*cb)(void *closure))
{
	return pollmgr.itf->on_readable(hndl, cb);
}

static inline int afb_pollmgr_on_writable(struct afb_pollmgr pollmgr, void *hndl, void (*cb)(void *closure))
{
	return pollmgr.itf->on_writable(hndl, cb);
}

static inline void afb_pollmgr_on_hangup(struct afb_pollmgr pollmgr, void *hndl, void (*cb)(void *closure))
{
	pollmgr.itf->on_hangup(hndl, cb);
}


