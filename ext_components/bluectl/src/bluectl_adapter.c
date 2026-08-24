/*
 * bluectl_adapter.c - 适配器操作(power/discoverable/pairable/scan...)
 */
#include "bluectl_internal.h"

#include <stdio.h>
#include <string.h>

struct bctl_list_adapters {
	bluectl_adapter_t *out;
	int max;
	int count;
};

static void list_adapters_cb(const char *path, const char *iface,
			     DBusMessageIter *props, void *user)
{
	struct bctl_list_adapters *l = user;

	if (strcmp(iface, IFACE_ADAPTER1))
		return;
	if (l->count >= l->max)
		return;
	bctl_strscpy(l->out[l->count].path, path, BLUECTL_PATH_LEN);
	bctl_adapter_fill(props, &l->out[l->count]);
	l->count++;
}

int bluectl_get_adapters(bluectl_adapter_t *out, int max)
{
	struct bluectl_ctx *c = &g_bluectl;
	struct bctl_list_adapters l = { out, max, 0 };
	int rv;

	if (!out || max <= 0) {
		bctl_set_err(c, BLUECTL_ERR_INVALID_ARG, "invalid args");
		return BLUECTL_ERR_INVALID_ARG;
	}
	rv = bctl_foreach_object(c, list_adapters_cb, &l);
	if (rv < 0)
		return rv;
	if (!l.count) {
		bctl_set_err(c, BLUECTL_ERR_NO_ADAPTER, "no bluetooth adapter found");
		return BLUECTL_ERR_NO_ADAPTER;
	}
	return l.count;
}

int bluectl_get_adapter(const char *adapter, bluectl_adapter_t *out)
{
	struct bluectl_ctx *c = &g_bluectl;
	char path[BLUECTL_PATH_LEN];
	DBusMessage *reply;
	DBusMessageIter root;
	int rv;

	if (!out)
		return BLUECTL_ERR_INVALID_ARG;
	rv = bctl_adapter_path(c, adapter, path, sizeof(path));
	if (rv < 0)
		return rv;
	reply = bctl_prop_get_all(c, path, IFACE_ADAPTER1);
	if (!reply)
		return c->err_code;
	if (!dbus_message_iter_init(reply, &root)) {
		dbus_message_unref(reply);
		bctl_set_err(c, BLUECTL_ERR, "GetAll: unexpected reply");
		return BLUECTL_ERR;
	}
	bctl_adapter_fill(&root, out);
	bctl_strscpy(out->path, path, sizeof(out->path));
	dbus_message_unref(reply);
	return BLUECTL_OK;
}

int bluectl_default_adapter(char *path, size_t len)
{
	return bctl_adapter_path(&g_bluectl, NULL, path, len);
}

static int adapter_prop_bool(const char *adapter, const char *name, int on)
{
	struct bluectl_ctx *c = &g_bluectl;
	char path[BLUECTL_PATH_LEN];
	dbus_bool_t v = on ? TRUE : FALSE;
	int rv;

	rv = bctl_adapter_path(c, adapter, path, sizeof(path));
	if (rv < 0)
		return rv;
	return bctl_prop_set(c, path, IFACE_ADAPTER1, name,
			     DBUS_TYPE_BOOLEAN, &v);
}

int bluectl_set_power(const char *adapter, int on)
{
	return adapter_prop_bool(adapter, "Powered", on);
}

int bluectl_set_pairable(const char *adapter, int on)
{
	return adapter_prop_bool(adapter, "Pairable", on);
}

int bluectl_set_discoverable(const char *adapter, int on)
{
	return adapter_prop_bool(adapter, "Discoverable", on);
}

static int adapter_prop_uint(const char *adapter, const char *name,
			     unsigned int seconds)
{
	struct bluectl_ctx *c = &g_bluectl;
	char path[BLUECTL_PATH_LEN];
	dbus_uint32_t v = seconds;
	int rv;

	rv = bctl_adapter_path(c, adapter, path, sizeof(path));
	if (rv < 0)
		return rv;
	return bctl_prop_set(c, path, IFACE_ADAPTER1, name,
			     DBUS_TYPE_UINT32, &v);
}

int bluectl_set_discoverable_timeout(const char *adapter, unsigned int seconds)
{
	return adapter_prop_uint(adapter, "DiscoverableTimeout", seconds);
}

int bluectl_set_pairable_timeout(const char *adapter, unsigned int seconds)
{
	return adapter_prop_uint(adapter, "PairableTimeout", seconds);
}

int bluectl_set_alias(const char *adapter, const char *alias)
{
	struct bluectl_ctx *c = &g_bluectl;
	char path[BLUECTL_PATH_LEN];
	int rv;

	if (!alias || !alias[0])
		return BLUECTL_ERR_INVALID_ARG;
	rv = bctl_adapter_path(c, adapter, path, sizeof(path));
	if (rv < 0)
		return rv;
	return bctl_prop_set(c, path, IFACE_ADAPTER1, "Alias",
			     DBUS_TYPE_STRING, &alias);
}

int bluectl_start_discovery(const char *adapter)
{
	struct bluectl_ctx *c = &g_bluectl;
	char path[BLUECTL_PATH_LEN];
	DBusMessage *reply;
	int rv;

	rv = bctl_adapter_path(c, adapter, path, sizeof(path));
	if (rv < 0)
		return rv;
	reply = bctl_call(c, path, IFACE_ADAPTER1, "StartDiscovery", -1);
	if (!reply) {
		/* 已在扫描不算错误 */
		if (strstr(c->err_name, "InProgress"))
			return BLUECTL_OK;
		return c->err_code;
	}
	dbus_message_unref(reply);
	return BLUECTL_OK;
}

int bluectl_stop_discovery(const char *adapter)
{
	struct bluectl_ctx *c = &g_bluectl;
	char path[BLUECTL_PATH_LEN];
	DBusMessage *reply;
	int rv;

	rv = bctl_adapter_path(c, adapter, path, sizeof(path));
	if (rv < 0)
		return rv;
	reply = bctl_call(c, path, IFACE_ADAPTER1, "StopDiscovery", -1);
	if (!reply) {
		/* 本就没在扫描不算错误(不同 bluez 版本错误名不一致) */
		if (strstr(c->err_name, "NotStarted") ||
		    strstr(c->err, "No discovery started"))
			return BLUECTL_OK;
		return c->err_code;
	}
	dbus_message_unref(reply);
	return BLUECTL_OK;
}

int bluectl_remove_device(const char *adapter, const char *device)
{
	struct bluectl_ctx *c = &g_bluectl;
	char adapter_path[BLUECTL_PATH_LEN];
	char device_path[BLUECTL_PATH_LEN];
	const char *dev_obj = device_path;
	DBusMessage *call;
	DBusMessageIter it;
	DBusMessage *reply;
	int rv;

	rv = bctl_adapter_path(c, adapter, adapter_path, sizeof(adapter_path));
	if (rv < 0)
		return rv;
	rv = bctl_device_path(c, device, device_path, sizeof(device_path));
	if (rv < 0)
		return rv;

	call = dbus_message_new_method_call(BLUEZ_NAME, adapter_path,
					    IFACE_ADAPTER1, "RemoveDevice");
	if (!call) {
		bctl_set_err(c, BLUECTL_ERR_NO_MEM, "no memory for message");
		return BLUECTL_ERR_NO_MEM;
	}
	dbus_message_iter_init_append(call, &it);
	if (!dbus_message_iter_append_basic(&it, DBUS_TYPE_OBJECT_PATH,
					    &dev_obj)) {
		dbus_message_unref(call);
		bctl_set_err(c, BLUECTL_ERR_NO_MEM, "message build failed");
		return BLUECTL_ERR_NO_MEM;
	}
	reply = bctl_send(c, call, -1);
	if (!reply)
		return c->err_code;
	dbus_message_unref(reply);
	return BLUECTL_OK;
}
