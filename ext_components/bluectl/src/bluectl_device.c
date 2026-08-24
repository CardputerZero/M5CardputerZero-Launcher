/*
 * bluectl_device.c - 设备操作(枚举/查询/pair/connect/trust/alias...)
 */
#include "bluectl_internal.h"

#include <stdio.h>
#include <string.h>

struct bctl_list_devices {
	struct bluectl_ctx *c;
	const char *prefix;	/* 按适配器过滤: 适配器对象路径, NULL = 全部 */
	bluectl_device_t *out;
	int max;
	int count;
};

static void list_devices_cb(const char *path, const char *iface,
			    DBusMessageIter *props, void *user)
{
	struct bctl_list_devices *l = user;
	bluectl_device_t dev;

	if (strcmp(iface, IFACE_DEVICE1))
		return;
	if (l->prefix && strncmp(path, l->prefix, strlen(l->prefix)))
		return;
	/* 先粗解析出 MAC, 过滤掉尚未拿到地址的临时对象 */
	bctl_device_fill(props, &dev);
	if (!dev.address[0])
		return;
	if (l->count >= l->max)
		return;
	l->out[l->count] = dev;
	bctl_strscpy(l->out[l->count].path, path, BLUECTL_PATH_LEN);
	l->count++;
}

int bluectl_get_devices(const char *adapter, bluectl_device_t *out, int max)
{
	struct bluectl_ctx *c = &g_bluectl;
	char adapter_path[BLUECTL_PATH_LEN];
	struct bctl_list_devices l = { c, NULL, out, max, 0 };
	int rv;

	if (!out || max <= 0) {
		bctl_set_err(c, BLUECTL_ERR_INVALID_ARG, "invalid args");
		return BLUECTL_ERR_INVALID_ARG;
	}
	if (adapter && adapter[0]) {
		rv = bctl_adapter_path(c, adapter, adapter_path,
				       sizeof(adapter_path));
		if (rv < 0)
			return rv;
		l.prefix = adapter_path;
	}
	rv = bctl_foreach_object(c, list_devices_cb, &l);
	if (rv < 0)
		return rv;
	return l.count;
}

int bluectl_get_device(const char *device, bluectl_device_t *out)
{
	struct bluectl_ctx *c = &g_bluectl;
	char path[BLUECTL_PATH_LEN];
	DBusMessage *reply;
	DBusMessageIter root;
	int rv;

	if (!out)
		return BLUECTL_ERR_INVALID_ARG;
	rv = bctl_device_path(c, device, path, sizeof(path));
	if (rv < 0)
		return rv;
	reply = bctl_prop_get_all(c, path, IFACE_DEVICE1);
	if (!reply)
		return c->err_code;
	if (!dbus_message_iter_init(reply, &root)) {
		dbus_message_unref(reply);
		bctl_set_err(c, BLUECTL_ERR, "GetAll: unexpected reply");
		return BLUECTL_ERR;
	}
	bctl_device_fill(&root, out);
	bctl_strscpy(out->path, path, sizeof(out->path));
	dbus_message_unref(reply);
	return BLUECTL_OK;
}

/* 设备方法调用(Pair/Connect/Disconnect) */
static int device_method(const char *device, const char *method, int timeout_ms)
{
	struct bluectl_ctx *c = &g_bluectl;
	char path[BLUECTL_PATH_LEN];
	DBusMessage *reply;
	int rv;

	rv = bctl_device_path(c, device, path, sizeof(path));
	if (rv < 0)
		return rv;
	reply = bctl_call(c, path, IFACE_DEVICE1, method, timeout_ms);
	if (!reply)
		return c->err_code;
	dbus_message_unref(reply);
	return BLUECTL_OK;
}

int bluectl_pair(const char *device)
{
	return device_method(device, "Pair", BLUECTL_PAIR_TIMEOUT_MS);
}

int bluectl_connect(const char *device)
{
	return device_method(device, "Connect", BLUECTL_CONNECT_TIMEOUT_MS);
}

int bluectl_disconnect(const char *device)
{
	return device_method(device, "Disconnect", -1);
}

static int device_prop_bool(const char *device, const char *name, int on)
{
	struct bluectl_ctx *c = &g_bluectl;
	char path[BLUECTL_PATH_LEN];
	dbus_bool_t v = on ? TRUE : FALSE;
	int rv;

	rv = bctl_device_path(c, device, path, sizeof(path));
	if (rv < 0)
		return rv;
	return bctl_prop_set(c, path, IFACE_DEVICE1, name,
			     DBUS_TYPE_BOOLEAN, &v);
}

int bluectl_set_trusted(const char *device, int on)
{
	return device_prop_bool(device, "Trusted", on);
}

int bluectl_set_blocked(const char *device, int on)
{
	return device_prop_bool(device, "Blocked", on);
}

int bluectl_set_device_alias(const char *device, const char *alias)
{
	struct bluectl_ctx *c = &g_bluectl;
	char path[BLUECTL_PATH_LEN];
	int rv;

	if (!alias || !alias[0])
		return BLUECTL_ERR_INVALID_ARG;
	rv = bctl_device_path(c, device, path, sizeof(path));
	if (rv < 0)
		return rv;
	return bctl_prop_set(c, path, IFACE_DEVICE1, "Alias",
			     DBUS_TYPE_STRING, &alias);
}
