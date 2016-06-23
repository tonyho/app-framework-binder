/*
 * Copyright (C) 2016 "IoT.bzh"
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
#include <stdio.h>
#include <string.h>
#include <json-c/json.h>

#include <systemd/sd-bus.h>
#include <systemd/sd-bus-protocol.h>

#include <afb/afb-binding.h>

/*
 * the interface to afb-daemon
 */
const struct afb_binding_interface *afbitf;

/*
 * union of possible dbus values
 */
union any {
	uint8_t u8;
	int16_t i16;
	uint16_t u16;
	int32_t i32;
	uint32_t u32;
	int64_t i64;
	uint64_t u64;
	double dbl;
	const char *cstr;
	char *str;
};

static int unpacklist(struct sd_bus_message *msg, struct json_object **result);
static int packlist(struct sd_bus_message *msg, const char *signature, struct json_object *list);

/*
 * Get the string of 'key' from 'obj'
 * Returns NULL if 'key' isn't in 'obj'
 */ 
static const char *strval(struct json_object *obj, const char *key)
{
	struct json_object *keyval;
	return json_object_object_get_ex(obj, key, &keyval) ? json_object_get_string(keyval) : NULL;
}

/*
 * Signature of a json object
 */
static const char *signature_for_json(struct json_object *obj)
{
	switch (json_object_get_type(obj)) {
	default:
	case json_type_null:
		return NULL;
	case json_type_boolean:
		return "b";
	case json_type_double:
		return "d";
	case json_type_int:
		return "i";
	case json_type_object:
		return "a{sv}";
	case json_type_array:
		return "av";
	case json_type_string:
		return "s";
	}
}

/*
 * Length of a single complete type
 */
static int lentype(const char *signature, int allows_dict, int allows_not_basic)
{
	int rc, len;
	switch(signature[0]) {

	case SD_BUS_TYPE_ARRAY:
		if (!allows_not_basic)
			break;
		rc = lentype(signature + 1, 1, 1);
		if (rc < 0)
			break;
		return 1 + rc;

	case SD_BUS_TYPE_STRUCT_BEGIN:
		if (!allows_not_basic)
			break;
		len = 1;
		rc = lentype(signature + len, 0, 1);
		while (rc > 0 && signature[len] != SD_BUS_TYPE_STRUCT_END) {
			len += rc;
			rc = lentype(signature + len, 0, 1);
		}
		if (rc < 0)
			break;
		return 1 + len;		

	case SD_BUS_TYPE_DICT_ENTRY_BEGIN:
		if (!allows_not_basic || !allows_dict)
			break;
		rc = lentype(signature + 1, 0, 0);
		if (rc < 0)
			break;
		len = 1 + rc;
		rc = lentype(signature + len, 0, 1);
		if (rc < 0 || signature[len + rc] != SD_BUS_TYPE_DICT_ENTRY_END)
			break;
		return len + rc + 1;

	case '\x0':
	case SD_BUS_TYPE_STRUCT:
	case SD_BUS_TYPE_STRUCT_END:
	case SD_BUS_TYPE_DICT_ENTRY:
	case SD_BUS_TYPE_DICT_ENTRY_END:
		break;

	default:
		return 1;
	}
	return -1;
}


/*
 * Unpack a D-Bus message to a json object
 */
static int unpacksingle(struct sd_bus_message *msg, struct json_object **result)
{
	char c;
	int rc;
	union any any;
	const char *content;
	struct json_object *item;

	*result = NULL;
	rc = sd_bus_message_peek_type(msg, &c, &content);
	if (rc <= 0)
		return rc;

	switch (c) {
	case SD_BUS_TYPE_BYTE:
	case SD_BUS_TYPE_BOOLEAN:
	case SD_BUS_TYPE_INT16:
	case SD_BUS_TYPE_UINT16:
	case SD_BUS_TYPE_INT32:
	case SD_BUS_TYPE_UINT32:
	case SD_BUS_TYPE_INT64:
	case SD_BUS_TYPE_UINT64:
	case SD_BUS_TYPE_DOUBLE:
	case SD_BUS_TYPE_STRING:
	case SD_BUS_TYPE_OBJECT_PATH:
	case SD_BUS_TYPE_SIGNATURE:
		rc = sd_bus_message_read_basic(msg, c, &any);
		if (rc < 0)
			goto error;
		switch (c) {
		case SD_BUS_TYPE_BOOLEAN:
			*result = json_object_new_boolean(any.i32);
			break;
		case SD_BUS_TYPE_BYTE:
			*result = json_object_new_int(any.u8);
			break;
		case SD_BUS_TYPE_INT16:
			*result = json_object_new_int(any.i16);
			break;
		case SD_BUS_TYPE_UINT16:
			*result = json_object_new_int(any.u16);
			break;
		case SD_BUS_TYPE_INT32:
			*result = json_object_new_int(any.i32);
			break;
		case SD_BUS_TYPE_UINT32:
			*result = json_object_new_int64(any.u32);
			break;
		case SD_BUS_TYPE_INT64:
			*result = json_object_new_int64(any.i64);
			break;
		case SD_BUS_TYPE_UINT64:
			*result = json_object_new_int64((int64_t)any.u64);
			break;
		case SD_BUS_TYPE_DOUBLE:
			*result = json_object_new_string(any.cstr);
			break;
		case SD_BUS_TYPE_STRING:
		case SD_BUS_TYPE_OBJECT_PATH:
		case SD_BUS_TYPE_SIGNATURE:
			*result = json_object_new_string(any.cstr);
			break;
		}
		return *result == NULL ? -1 : 1;

	case SD_BUS_TYPE_ARRAY:
	case SD_BUS_TYPE_VARIANT:
	case SD_BUS_TYPE_STRUCT:
	case SD_BUS_TYPE_DICT_ENTRY:
		rc = sd_bus_message_enter_container(msg, c, content);
		if (rc < 0)
			goto error;
		if (c == SD_BUS_TYPE_ARRAY && content[0] == SD_BUS_TYPE_DICT_ENTRY_BEGIN && content[1] == SD_BUS_TYPE_STRING) {
			*result = json_object_new_object();
			if (*result == NULL)
				return -1;
			for(;;) {
				rc = sd_bus_message_enter_container(msg, 0, NULL);
				if (rc < 0)
					goto error;
				if (rc == 0)
					break;
				rc = sd_bus_message_read_basic(msg, SD_BUS_TYPE_STRING, &any);
				if (rc < 0)
					goto error;
				rc = unpacksingle(msg, &item);
				if (rc < 0)
					goto error;
				json_object_object_add(*result, any.cstr, item);
				rc = sd_bus_message_exit_container(msg);
				if (rc < 0)
					goto error;
			}
		} else {
			rc = unpacklist(msg, result);
			if (rc < 0)
				goto error;
		}
		rc = sd_bus_message_exit_container(msg);
		if (rc < 0)
			goto error;
		return 1;
	default:
		goto error;
	}
error:
	json_object_put(*result);
	return -1;
}

/*
 * Unpack a D-Bus message to a json object
 */
static int unpacklist(struct sd_bus_message *msg, struct json_object **result)
{
	int rc;
	struct json_object *item;

	/* allocates the result */
	*result = json_object_new_array();
	if (*result == NULL)
		goto error;

	/* read the values */
	for (;;) {
		rc = unpacksingle(msg, &item);
		if (rc < 0)
			goto error;
		if (rc == 0)
			return 0;
		json_object_array_add(*result, item);
	}
error:
	json_object_put(*result);
	*result = NULL;
	return -1;
}

static int packsingle(struct sd_bus_message *msg, const char *signature, struct json_object *item)
{
	int index, count, rc, len;
	union any any;
	char *subsig;
	struct json_object_iterator it, end;

	len = lentype(signature, 0, 1);
	if (len < 0)
		goto error;

	switch (*signature) {
	case SD_BUS_TYPE_BOOLEAN:
		any.i32 = json_object_get_boolean(item);
		break;

	case SD_BUS_TYPE_BYTE:
		any.i32 = json_object_get_int(item);
		if (any.i32 != (int32_t)(uint8_t)any.i32)
			goto error;
		any.u8 = (uint8_t)any.i32;
		break;

	case SD_BUS_TYPE_INT16:
		any.i32 = json_object_get_int(item);
		if (any.i32 != (int32_t)(int16_t)any.i32)
			goto error;
		any.i16 = (int16_t)any.i32;
		break;

	case SD_BUS_TYPE_UINT16:
		any.i32 = json_object_get_int(item);
		if (any.i32 != (int32_t)(uint16_t)any.i32)
			goto error;
		any.u16 = (uint16_t)any.i32;
		break;

	case SD_BUS_TYPE_INT32:
		any.i64 = json_object_get_int64(item);
		if (any.i64 != (int64_t)(int32_t)any.i64)
			goto error;
		any.i32 = (int32_t)any.i64;
		break;

	case SD_BUS_TYPE_UINT32:
		any.i64 = json_object_get_int64(item);
		if (any.i64 != (int64_t)(uint32_t)any.i64)
			goto error;
		any.u32 = (uint32_t)any.i64;
		break;

	case SD_BUS_TYPE_INT64:
		any.i64 = json_object_get_int64(item);
		break;

	case SD_BUS_TYPE_UINT64:
		any.u64 = (uint64_t)json_object_get_int64(item);
		break;

	case SD_BUS_TYPE_DOUBLE:
		any.dbl = json_object_get_double(item);
		break;

	case SD_BUS_TYPE_STRING:
	case SD_BUS_TYPE_OBJECT_PATH:
	case SD_BUS_TYPE_SIGNATURE:
		any.cstr = json_object_get_string(item);
		break;

	case SD_BUS_TYPE_VARIANT:
		signature = signature_for_json(item);
		if (signature == NULL)
			goto error;
		rc = sd_bus_message_open_container(msg, SD_BUS_TYPE_VARIANT, signature);
		if (rc < 0)
			goto error;
		rc = packsingle(msg, signature, item);
		if (rc < 0)
			goto error;
		rc = sd_bus_message_close_container(msg);
		if (rc < 0)
			goto error;
		return len;

	case SD_BUS_TYPE_ARRAY:
		subsig = strndupa(signature + 1, len - 1);
		rc = sd_bus_message_open_container(msg, SD_BUS_TYPE_ARRAY, subsig);
		if (rc < 0)
			goto error;
		if (json_object_is_type(item, json_type_array)) {
			/* Is an array! */
			count = json_object_array_length(item);
			index = 0;
			while(index < count) {
				rc = packsingle(msg, subsig, json_object_array_get_idx(item, index++));
				if (rc < 0)
					goto error;
			}
		} else {
			/* Not an array! Check if it matches an string dictionnary */
			if (!json_object_is_type(item, json_type_object))
				goto error;
			if (*subsig++ != SD_BUS_TYPE_DICT_ENTRY_BEGIN)
				goto error;
			if (*subsig != SD_BUS_TYPE_STRING)
				goto error;
			/* iterate the object values */
			subsig[strlen(subsig) - 1] = 0;
			it = json_object_iter_begin(item);
			end = json_object_iter_end(item);
			while (!json_object_iter_equal(&it, &end)) {
				rc = sd_bus_message_open_container(msg, SD_BUS_TYPE_DICT_ENTRY, subsig);
				if (rc < 0)
					goto error;
				any.cstr = json_object_iter_peek_name(&it);
				rc = sd_bus_message_append_basic(msg, *subsig, &any);
				if (rc < 0)
					goto error;
				rc = packsingle(msg, subsig + 1, json_object_iter_peek_value(&it));
				if (rc < 0)
					goto error;
				rc = sd_bus_message_close_container(msg);
				if (rc < 0)
					goto error;
				json_object_iter_next(&it);
			}
		}
		rc = sd_bus_message_close_container(msg);
		if (rc < 0)
			goto error;
		return len;

	case SD_BUS_TYPE_STRUCT_BEGIN:
	case SD_BUS_TYPE_DICT_ENTRY_BEGIN:
		subsig = strndupa(signature + 1, len - 2);
		rc = sd_bus_message_open_container(msg,
			((*signature) == SD_BUS_TYPE_STRUCT_BEGIN) ? SD_BUS_TYPE_STRUCT : SD_BUS_TYPE_DICT_ENTRY,
			subsig);
		if (rc < 0)
			goto error;
		rc = packlist(msg, subsig, item);
		if (rc < 0)
			goto error;
		rc = sd_bus_message_close_container(msg);
		if (rc < 0)
			goto error;
		return len;

	default:
		goto error;
	}

	rc = sd_bus_message_append_basic(msg, *signature, &any);
	if (rc < 0)
		goto error;
	return len;

error:
	return -1;
}

static int packlist(struct sd_bus_message *msg, const char *signature, struct json_object *list)
{
	int rc, count, index, scan;
	struct json_object *item;

	scan = 0;
	if (list == NULL) {
		/* empty case */
		if (*signature)
			goto error;
		return scan;
	}

	if (!json_object_is_type(list, json_type_array)) {
		/* down grade gracefully to single */
		rc = packsingle(msg, signature, list);
		if (rc < 0)
			goto error;
		scan = rc;
		if (signature[scan] != 0)
			goto error;
		return scan;
	}

	/* iterate over elements */
	count = json_object_array_length(list);
	index = 0;
	for (;;) {
		/* check state */
		if (index == count && signature[scan] == 0)
			return scan;
		if (index == count || signature[scan] == 0)
			goto error;

		/* get the item */
		item = json_object_array_get_idx(list, index);
		if (item == NULL)
			goto error;

		/* pack the item */
		rc = packsingle(msg, signature + scan, item);
		if (rc < 0)
			goto error;

		/* advance */
		scan += rc;
		index++;
	}

error:
	return -(scan + 1);
}

/*
 * handle the reply
 */
static int on_rawcall_reply(sd_bus_message *msg, struct afb_req *req, sd_bus_error *ret_error)
{
	struct json_object *obj = NULL;
	int rc;
	const sd_bus_error *err;

	err = sd_bus_message_get_error(msg);
	if (err != NULL)
		afb_req_fail_f(*req, "failed", "DBus-error-name: %s, DBus-error-message: %s", err->name, err->message);
	else {
		rc = unpacklist(msg, &obj);
		if (rc < 0)
			afb_req_fail(*req, "failed", "can't unpack");
		else
			afb_req_success(*req, obj, NULL);
	}
	json_object_put(obj);
	afb_req_unref(*req);
	free(req);
	return 1;
}

/*
 * Make a raw call to DBUS method
 * The query should have:
 *   {
 *     "bus": "optional: 'system' or 'user' (default)"
 *     "destination": "destination handling the object",
 *     "path": "object path",
 *     "interface": "interface of the call",
 *     "member": "member of the interface of the call",
 *     "signature": "signature of the arguments",
 *     "arguments": "ARRAY of arguments"
 *   }
 */
static void rawcall(struct afb_req req)
{
	struct json_object *obj;
	struct json_object *args;

	const char *busname;
	const char *destination;
	const char *path;
	const char *interface;
	const char *member;
	const char *signature;

	struct sd_bus_message *msg = NULL;
	struct sd_bus *bus;
	int rc;

	/* get the query */
	obj = afb_req_json(req);
	if (obj == NULL)
		goto internal_error;

	/* get parameters */
	destination = strval(obj, "destination");
	path = strval(obj, "path");
	interface = strval(obj, "interface");
	member = strval(obj, "member");
	if (path == NULL || member == NULL)
		goto bad_request;

	/* get arguments */
	signature = strval(obj, "signature") ? : "";
	args = NULL;
	json_object_object_get_ex(obj, "arguments", &args);

	/* get bus */
	busname = strval(obj, "bus");
	if (busname != NULL && !strcmp(busname, "system"))
		bus = afb_daemon_get_system_bus(afbitf->daemon);
	else
		bus = afb_daemon_get_user_bus(afbitf->daemon);

	/* creates the message */
	rc = sd_bus_message_new_method_call(bus, &msg, destination, path, interface, member);
	if (rc != 0)
		goto internal_error;
	rc = packlist(msg, signature, args);
	if (rc < 0)
		goto bad_request;

	/*  */
	rc = sd_bus_call_async(bus, NULL, msg, (void*)on_rawcall_reply, afb_req_store(req), -1);
	if (rc < 0)
		goto internal_error;
	goto cleanup;

internal_error:
	afb_req_fail(req, "failed", "internal error");
	goto cleanup;

bad_request:
	afb_req_fail(req, "failed", "bad request");

cleanup:
	sd_bus_message_unref(msg);
}

/*
 * array of the verbs exported to afb-daemon
 */
static const struct afb_verb_desc_v1 binding_verbs[] = {
  /* VERB'S NAME       SESSION MANAGEMENT          FUNCTION TO CALL     SHORT DESCRIPTION */
  { .name= "rawcall",  .session= AFB_SESSION_NONE, .callback= rawcall,  .info= "raw call to dbus method" },
  { .name= NULL } /* marker for end of the array */
};

/*
 * description of the binding for afb-daemon
 */
static const struct afb_binding binding_description =
{
  /* description conforms to VERSION 1 */
  .type= AFB_BINDING_VERSION_1,
  .v1= {			/* fills the v1 field of the union when AFB_BINDING_VERSION_1 */
    .prefix= "dbus",		/* the API name (or binding name or prefix) */
    .info= "raw dbus binding",	/* short description of of the binding */
    .verbs = binding_verbs	/* the array describing the verbs of the API */
  }
};

/*
 * activation function for registering the binding called by afb-daemon
 */
const struct afb_binding *afbBindingV1Register(const struct afb_binding_interface *itf)
{
	afbitf = itf;			/* records the interface for accessing afb-daemon */
	return &binding_description;	/* returns the description of the binding */
}

