/*
 * bluectl_device.c - 设备操作(枚举/查询/pair/connect/trust/alias...)
 */
#include "bluectl_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int device_path_compare(const void *a, const void *b)
{
	const bluectl_device_t *da = a;
	const bluectl_device_t *db = b;

	return strcmp(da->path, db->path);
}

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
	if (l->prefix) {
		size_t prefix_len = strlen(l->prefix);
		size_t path_len = strlen(path);

		/* 适配器前缀必须落在对象路径边界，避免 hci0 匹配 hci01。 */
		if (path_len < prefix_len || strncmp(path, l->prefix, prefix_len) ||
		    (path[prefix_len] != '/' && path[prefix_len] != '\0'))
			return;
	}
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
	/* ObjectManager 不承诺字典迭代顺序, 对外 API 承诺按路径排序。 */
	if (l.count > 1)
		qsort(out, (size_t)l.count, sizeof(*out), device_path_compare);
	return l.count;
}

int bluectl_get_device(const char *device, bluectl_device_t *out)
{
	struct bluectl_ctx *c = &g_bluectl;
	char path[BLUECTL_PATH_LEN];
	DBusMessage *reply;
	DBusMessageIter root;
	int rv;

	if (!out) {
		bctl_set_err(c, BLUECTL_ERR_INVALID_ARG, "output buffer is required");
		return BLUECTL_ERR_INVALID_ARG;
	}
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

/*
 * Pairing can leave an Agent1 request pending when BlueZ or the transport
 * fails before it sends Agent1.Cancel.  Clear that request at every Pair
 * failure boundary so a later pairing attempt cannot inherit stale state.
 */
static void pair_cancel_agent(void)
{
#ifdef CONFIG_BLUECTL_AGENT_ENABLED
	(void)bluectl_agent_cancel_pending();
#endif
}

int bluectl_pair(const char *device)
{
	struct bluectl_ctx *c = &g_bluectl;
	char path[BLUECTL_PATH_LEN];
	DBusMessage *call;
	DBusPendingCall *pending = NULL;
	DBusMessage *reply;
	DBusError err;
	struct timespec started;
	int rv;

	/* Pair may synchronously invoke Agent1.  A blocking libdbus call holds
	 * the connection lock while waiting, preventing the agent reply from
	 * being written by the process pump.  Use a pending call and poll it so
	 * the pump remains the sole dispatcher. */
	rv = bctl_device_path(c, device, path, sizeof(path));
	if (rv < 0)
		return rv;
	call = dbus_message_new_method_call(BLUEZ_NAME, path, IFACE_DEVICE1, "Pair");
	if (!call) {
		bctl_set_err(c, BLUECTL_ERR_NO_MEM, "no memory for Pair message");
		return BLUECTL_ERR_NO_MEM;
	}
	if (!dbus_connection_send_with_reply(c->conn, call, &pending,
					     BLUECTL_PAIR_TIMEOUT_MS)) {
		dbus_message_unref(call);
		pair_cancel_agent();
		bctl_set_err(c, BLUECTL_ERR_NO_MEM, "send Pair failed");
		return BLUECTL_ERR_NO_MEM;
	}
	dbus_message_unref(call);
	if (!pending) {
		pair_cancel_agent();
		bctl_set_err(c, BLUECTL_ERR, "Pair did not create pending call");
		return BLUECTL_ERR;
	}
	clock_gettime(CLOCK_MONOTONIC, &started);
	while (!dbus_pending_call_get_completed(pending)) {
		struct timespec now;
		long long elapsed_ms;

		/* The caller is expected to run bluectl_process() in another thread. */
		usleep(1000);
		clock_gettime(CLOCK_MONOTONIC, &now);
		elapsed_ms = (long long)(now.tv_sec - started.tv_sec) * 1000LL +
			(long long)(now.tv_nsec - started.tv_nsec) / 1000000LL;
		if (elapsed_ms >= BLUECTL_PAIR_TIMEOUT_MS)
			break;
	}
	if (!dbus_pending_call_get_completed(pending)) {
		dbus_pending_call_cancel(pending);
		dbus_pending_call_unref(pending);
		pair_cancel_agent();
		bctl_set_err(c, BLUECTL_ERR_TIMEOUT, "Pair timed out");
		return BLUECTL_ERR_TIMEOUT;
	}
	reply = dbus_pending_call_steal_reply(pending);
	dbus_pending_call_unref(pending);
	if (!reply) {
		pair_cancel_agent();
		bctl_set_err(c, BLUECTL_ERR, "Pair returned no reply");
		return BLUECTL_ERR;
	}
	if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
		dbus_error_init(&err);
		if (!dbus_set_error_from_message(&err, reply))
			dbus_set_error_const(&err, dbus_message_get_error_name(reply),
				     "Pair failed");
		/* Do cleanup before recording the Pair error; cancellation can
		 * observe a concurrently lost connection and update c->err. */
		pair_cancel_agent();
		rv = bctl_set_dbus_err(c, &err);
		dbus_error_free(&err);
		dbus_message_unref(reply);
		return rv;
	}
	dbus_message_unref(reply);
	return BLUECTL_OK;
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

	if (!alias || !alias[0]) {
		bctl_set_err(c, BLUECTL_ERR_INVALID_ARG, "alias is required");
		return BLUECTL_ERR_INVALID_ARG;
	}
	rv = bctl_device_path(c, device, path, sizeof(path));
	if (rv < 0)
		return rv;
	return bctl_prop_set(c, path, IFACE_DEVICE1, "Alias",
			     DBUS_TYPE_STRING, &alias);
}
