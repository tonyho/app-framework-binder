/*
 * Copyright (C) 2015, 2016 "IoT.bzh"
 * Author "Fulup Ar Foll"
 * Author José Bollo <jose.bollo@iot.bzh>
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

#define _GNU_SOURCE

#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <setjmp.h>

#include <afb/afb-req-itf.h>

#include "afb-sig-handler.h"
#include "afb-thread.h"
#include "verbose.h"

static _Thread_local sigjmp_buf *error_handler;

static void on_signal_terminate (int signum)
{
	ERROR("Terminating signal received %s", strsignal(signum));
	exit(1);
}

static void on_signal_error(int signum)
{
	sigset_t sigset;

	// unlock signal to allow a new signal to come
	if (error_handler != NULL) {
		sigemptyset(&sigset);
		sigaddset(&sigset, signum);
		sigprocmask(SIG_UNBLOCK, &sigset, 0);
		longjmp(*error_handler, signum);
	}
	if (signum == SIGALRM)
		return;
	ERROR("Unmonitored signal received %s", strsignal(signum));
	exit(2);
}

static int install(void (*handler)(int), int *signals)
{
	int result = 1;
	while(*signals > 0) {
		if (signal(*signals, handler) == SIG_ERR) {
			ERROR("failed to install signal handler for signal %s", strsignal(*signals));
			result = 0;
		}
		signals++;
	}
	return result;
}

int afb_sig_handler_init()
{
	static int sigerr[] = { SIGALRM, SIGSEGV, SIGFPE, 0 };
	static int sigterm[] = { SIGINT, SIGABRT, 0 };

	return (install(on_signal_error, sigerr) & install(on_signal_terminate, sigterm)) - 1;
}

int afb_sig_req(struct afb_req req, void (*callback)(struct afb_req req))
{
	volatile int signum;
	sigjmp_buf jmpbuf, *older;

	older = error_handler;
	signum = setjmp(jmpbuf);
	if (signum != 0)
		afb_req_fail_f(req, "aborted", "signal %s(%d) caught", strsignal(signum), signum);
	else {
		error_handler = &jmpbuf;
		callback(req);
	}
	error_handler = older;
	return signum;
}

int afb_sig_req_timeout(struct afb_req req, void (*callback)(struct afb_req req), int timeout)
{
	int rc;

	if (timeout)
		afb_thread_timer_arm(timeout);
	rc = afb_sig_req(req, callback);
	afb_thread_timer_disarm();
	return rc;
}

void afb_sig_monitor(void (*function)(int sig, void*), void *closure, int timeout)
{
	volatile int signum;
	sigjmp_buf jmpbuf, *older;

	older = error_handler;
	signum = setjmp(jmpbuf);
	if (signum != 0) {
		function(signum, closure);
	}
	else {
		error_handler = &jmpbuf;
		if (timeout)
			afb_thread_timer_arm(timeout);
		function(0, closure);
	}
	afb_thread_timer_disarm();
	error_handler = older;
}

