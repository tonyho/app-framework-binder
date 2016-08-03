#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <afb/afb-req-itf.h>
#include "../afb-thread.h"

struct foo {
	int value;
	int refcount;
};

void addref(void *closure)
{
	struct foo *foo = closure;
	foo->refcount++;
}

void unref(void *closure)
{
	struct foo *foo = closure;
	if(!--foo->refcount) {
		printf("%06d FREE\n", foo->value);
		free(foo);
	}
}

void fail(void *closure, const char *status, const char *info)
{
	struct foo *foo = closure;
	printf("%06d ERROR %s\n", foo->value, status);
}

struct afb_req_itf itf = {
	.json = NULL,
	.get = NULL,

	.success = NULL,
	.fail = fail,

	.raw = NULL,
	.send = NULL,

	.context_get = NULL,
	.context_set = NULL,

	.addref = addref,
	.unref = unref,

	.session_close = NULL,
	.session_set_LOA = NULL,

	.subscribe = NULL,
	.unsubscribe = NULL,

	.subcall = NULL
};

void process(struct afb_req req)
{
	struct timespec ts;
	struct foo *foo = req.closure;
	printf("%06d PROCESS T%d\n", foo->value, (int)syscall(SYS_gettid));
	ts.tv_sec = 0;
	ts.tv_nsec = foo->value * 1000;
//	nanosleep(&ts, NULL);
}

int main()
{
	int i;
	struct foo *foo;
	struct afb_req req;
	struct timespec ts;

	req.itf = &itf;
	afb_thread_init(4, 1);
	for (i = 0 ; i  < 10000 ; i++) {
		req.closure = foo = malloc(sizeof *foo);
		foo->value = i;
		foo->refcount = 1;
		afb_thread_call(req, process, 5, (&ts) + (i % 4));
		unref(foo);
		ts.tv_sec = 0;
		ts.tv_nsec = 1000000;
//		nanosleep(&ts, NULL);
	}
	ts.tv_sec = 1;
	ts.tv_nsec = 0;
	nanosleep(&ts, NULL);
	afb_thread_terminate();
}




