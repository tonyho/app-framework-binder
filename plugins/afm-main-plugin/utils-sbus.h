/*
 Copyright 2015 IoT.bzh

 author: José Bollo <jose.bollo@iot.bzh>

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

#pragma once

struct sbus;
struct sbusmsg;
struct sbus_signal;
struct sbus_service;

struct sbus_itf
{
	int (*wait)(int timeout, void *itfclo);
	void *(*open)(int fd, void *closure, void *itfclo);
	int (*on_readable)(void *hndl, void (*callback)(void *closure));
	int (*on_writable)(void *hndl, void (*callback)(void *closure));
	void (*on_hangup)(void *hndl, void (*callback)(void *closure));
	void (*close)(void *hndl);
};

extern struct sbus *sbus_session(const struct sbus_itf *itf, void *itfclo);
extern struct sbus *sbus_system(const struct sbus_itf *itf, void *itfclo);

extern void sbus_addref(struct sbus *sbus);
extern void sbus_unref(struct sbus *sbus);

extern int sbus_send_signal(struct sbus *sbus, const char *sender, const char *path, const char *iface, const char *name, const char *content);

extern int sbus_call(
		struct sbus *sbus,
		const char *destination,
		const char *path,
		const char *iface,
		const char *method,
		const char *query,
		void (*onresp) (int, const char *, void *),
		void *closure);

extern char *sbus_call_sync(
		struct sbus *sbus,
		const char *destination,
		const char *path,
		const char *iface,
		const char *method,
		const char *query);

extern struct sbus_signal *sbus_add_signal(
		struct sbus *sbus,
		const char *sender,
		const char *path,
		const char *iface,
		const char *name,
		void (*onsignal) (const struct sbusmsg *, const char *, void *),
		void *closure);

extern int sbus_remove_signal(struct sbus *sbus, struct sbus_signal *signal);

extern int sbus_add_name(struct sbus *sbus, const char *name);

extern struct sbus_service *sbus_add_service(
		struct sbus *sbus,
		const char *destination,
		const char *path,
		const char *iface,
		const char *member,
		void (*oncall) (struct sbusmsg *, const char *, void *),
		void *closure);

extern int sbus_remove_service(struct sbus *sbus, struct sbus_service *service);

extern const char *sbus_sender(const struct sbusmsg *smsg);

extern const char *sbus_destination(const struct sbusmsg *smsg);

extern const char *sbus_path(const struct sbusmsg *smsg);

extern const char *sbus_interface(const struct sbusmsg *smsg);

extern const char *sbus_member(const struct sbusmsg *smsg);

extern int sbus_reply_error(struct sbusmsg *smsg, const char *error);
extern int sbus_reply(struct sbusmsg *smsg, const char *reply);

