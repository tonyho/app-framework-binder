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
#include <unistd.h>
#include <time.h>
#include <sys/syscall.h>
#include <setjmp.h>

#include "afb-sig-handler.h"
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

void afb_sig_monitor(void (*function)(int sig, void*), void *closure, int timeout)
{
	volatile int signum, timerset;
	timer_t timerid;
	sigjmp_buf jmpbuf, *older;
	struct sigevent sevp;
	struct itimerspec its;

	timerset = 0;
	older = error_handler;
	signum = setjmp(jmpbuf);
	if (signum != 0) {
		function(signum, closure);
	}
	else {
		error_handler = &jmpbuf;
		if (timeout > 0) {
			timerset = 1; /* TODO: check statuses */
			sevp.sigev_notify = SIGEV_THREAD_ID;
			sevp.sigev_signo = SIGALRM;
			sevp.sigev_value.sival_ptr = NULL;
#if defined(sigev_notify_thread_id)
			sevp.sigev_notify_thread_id = (pid_t)syscall(SYS_gettid);
#else
			sevp._sigev_un._tid = (pid_t)syscall(SYS_gettid);
#endif
			timer_create(CLOCK_THREAD_CPUTIME_ID, &sevp, &timerid);
			its.it_interval.tv_sec = 0;
			its.it_interval.tv_nsec = 0;
			its.it_value.tv_sec = timeout;
			its.it_value.tv_nsec = 0;
			timer_settime(timerid, 0, &its, NULL);
		}

		function(0, closure);
	}
	if (timerset)
		timer_delete(timerid);
	error_handler = older;
}

