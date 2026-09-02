/*
 * bluectl_core.c - DBus 连接管理/事件泵/错误处理/公共工具
 */
#include "bluectl_internal.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

struct bluectl_ctx g_bluectl;

static const char *bctl_match_rules[] = {
	"type='signal',sender='" BLUEZ_NAME "',interface='" IFACE_OM "'",
	"type='signal',sender='" BLUEZ_NAME "',interface='" IFACE_PROPS "'",
	"type='signal',sender='org.freedesktop.DBus',"
	"interface='org.freedesktop.DBus',member='NameOwnerChanged',"
	"arg0='" BLUEZ_NAME "'",
};

#define BCTL_N_MATCH_RULES (int)(sizeof(bctl_match_rules) / sizeof(bctl_match_rules[0]))

/* ---------------- 基础工具 ---------------- */

void bctl_strscpy(char *dst, const char *src, size_t len)
{
	size_t n;

	if (!dst || !len)
		return;
	if (!src) {
		dst[0] = '\0';
		return;
	}
	n = strlen(src);
	if (n >= len)
		n = len - 1;
	memcpy(dst, src, n);
	dst[n] = '\0';
}

void bctl_set_err(struct bluectl_ctx *c, int code, const char *fmt, ...)
{
	va_list ap;

	c->err_code = code;
	va_start(ap, fmt);
	vsnprintf(c->err, sizeof(c->err), fmt, ap);
	va_end(ap);
	c->err_name[0] = '\0';
}

int bctl_set_dbus_err(struct bluectl_ctx *c, const DBusError *err)
{
	const char *name = (err && err->name) ? err->name : "";
	const char *msg = (err && err->message) ? err->message : "dbus call failed";
	int code = BLUECTL_ERR;

	if (strstr(name, "NoReply") || strstr(name, "Timeout") ||
	    strstr(name, "TimedOut"))
		code = BLUECTL_ERR_TIMEOUT;
	else if (strstr(name, "DoesNotExist") || strstr(name, "UnknownObject") ||
		 strstr(name, "UnknownMethod") || strstr(name, "NotFound"))
		code = BLUECTL_ERR_NOT_FOUND;
	else if (strstr(name, "InvalidArgs"))
		code = BLUECTL_ERR_INVALID_ARG;
	else if (strstr(name, "NoMemory"))
		code = BLUECTL_ERR_NO_MEM;
	else if (strstr(name, "Disconnected") || strstr(name, "NoConnection"))
		code = BLUECTL_ERR_NO_CONN;

	c->err_code = code;
	bctl_strscpy(c->err, msg, sizeof(c->err));
	bctl_strscpy(c->err_name, name, sizeof(c->err_name));
	return code;
}

int bctl_check_conn(struct bluectl_ctx *c)
{
	if (!c->conn || !dbus_connection_get_is_connected(c->conn)) {
		bctl_set_err(c, BLUECTL_ERR_NO_CONN,
			     "bluectl not connected (call bluectl_init first)");
		return 0;
	}
	return 1;
}

/* ---------------- 方法调用封装 ---------------- */

DBusMessage *bctl_send(struct bluectl_ctx *c, DBusMessage *call, int timeout_ms)
{
	DBusMessage *reply;
	DBusError err;

	if (!call) {
		bctl_set_err(c, BLUECTL_ERR_NO_MEM, "no memory for message");
		return NULL;
	}
	if (!bctl_check_conn(c)) {
		dbus_message_unref(call);
		return NULL;
	}
	if (timeout_ms < 0) {
		/* 公共 setter 接受 unsigned，但 libdbus 超时参数是 int。 */
		timeout_ms = c->timeout_ms > (unsigned int)INT_MAX ? INT_MAX :
			(int)c->timeout_ms;
	}

	dbus_error_init(&err);
	reply = dbus_connection_send_with_reply_and_block(c->conn, call,
							  timeout_ms, &err);
	dbus_message_unref(call);
	if (!reply) {
		bctl_set_dbus_err(c, &err);
		dbus_error_free(&err);
		return NULL;
	}
	return reply;
}

DBusMessage *bctl_call(struct bluectl_ctx *c, const char *path,
		       const char *iface, const char *method, int timeout_ms)
{
	DBusMessage *call;

	if (!path || !iface || !method) {
		bctl_set_err(c, BLUECTL_ERR_INVALID_ARG, "invalid call args");
		return NULL;
	}
	call = dbus_message_new_method_call(BLUEZ_NAME, path, iface, method);
	if (!call) {
		bctl_set_err(c, BLUECTL_ERR_NO_MEM, "no memory for message");
		return NULL;
	}
	return bctl_send(c, call, timeout_ms);
}

int bctl_prop_set(struct bluectl_ctx *c, const char *path, const char *iface,
		  const char *name, int type, const void *value)
{
	DBusMessage *call;
	DBusMessageIter it, var;
	char sig[2] = { (char)type, '\0' };
	DBusMessage *reply;
	int ok;

	if (!path || !iface || !name || !value) {
		bctl_set_err(c, BLUECTL_ERR_INVALID_ARG, "invalid property args");
		return BLUECTL_ERR_INVALID_ARG;
	}
	call = dbus_message_new_method_call(BLUEZ_NAME, path, IFACE_PROPS, "Set");
	if (!call) {
		bctl_set_err(c, BLUECTL_ERR_NO_MEM, "no memory for message");
		return BLUECTL_ERR_NO_MEM;
	}
	dbus_message_iter_init_append(call, &it);
	ok = dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &iface) &&
	     dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &name) &&
	     dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, sig, &var) &&
	     dbus_message_iter_append_basic(&var, type, (void *)value) &&
	     dbus_message_iter_close_container(&it, &var);
	if (!ok) {
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

DBusMessage *bctl_prop_get_all(struct bluectl_ctx *c, const char *path,
			       const char *iface)
{
	DBusMessage *call;
	DBusMessageIter it;

	if (!path || !path[0] || !iface || !iface[0]) {
		bctl_set_err(c, BLUECTL_ERR_INVALID_ARG, "invalid property args");
		return NULL;
	}
	call = dbus_message_new_method_call(BLUEZ_NAME, path, IFACE_PROPS, "GetAll");
	if (!call) {
		bctl_set_err(c, BLUECTL_ERR_NO_MEM, "no memory for message");
		return NULL;
	}
	dbus_message_iter_init_append(call, &it);
	if (!dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &iface)) {
		dbus_message_unref(call);
		bctl_set_err(c, BLUECTL_ERR_NO_MEM, "message build failed");
		return NULL;
	}
	return bctl_send(c, call, -1);
}

/* ---------------- 迭代器读取工具 ---------------- */

int bctl_read_bool(DBusMessageIter *it, int def)
{
	dbus_bool_t v;

	if (!it || dbus_message_iter_get_arg_type(it) != DBUS_TYPE_BOOLEAN)
		return def;
	dbus_message_iter_get_basic(it, &v);
	return v ? 1 : 0;
}

long bctl_read_num(DBusMessageIter *it, long def)
{
	if (!it)
		return def;

	switch (dbus_message_iter_get_arg_type(it)) {
	case DBUS_TYPE_BYTE: {
		unsigned char v;
		dbus_message_iter_get_basic(it, &v);
		return v;
	}
	case DBUS_TYPE_INT16: {
		dbus_int16_t v;
		dbus_message_iter_get_basic(it, &v);
		return v;
	}
	case DBUS_TYPE_UINT16: {
		dbus_uint16_t v;
		dbus_message_iter_get_basic(it, &v);
		return v;
	}
	case DBUS_TYPE_INT32: {
		dbus_int32_t v;
		dbus_message_iter_get_basic(it, &v);
		return v;
	}
	case DBUS_TYPE_UINT32: {
		dbus_uint32_t v;
		dbus_message_iter_get_basic(it, &v);
		return v;
	}
	case DBUS_TYPE_INT64: {
		dbus_int64_t v;
		dbus_message_iter_get_basic(it, &v);
		return v;
	}
	case DBUS_TYPE_UINT64: {
		dbus_uint64_t v;
		dbus_message_iter_get_basic(it, &v);
		return v;
	}
	default:
		return def;
	}
}

void bctl_read_str(DBusMessageIter *it, char *out, size_t len)
{
	const char *s = NULL;

	if (!out || !len)
		return;
	out[0] = '\0';
	if (!it)
		return;
	switch (dbus_message_iter_get_arg_type(it)) {
	case DBUS_TYPE_STRING:
	case DBUS_TYPE_OBJECT_PATH:
	case DBUS_TYPE_SIGNATURE:
		dbus_message_iter_get_basic(it, &s);
		if (s)
			bctl_strscpy(out, s, len);
		break;
	default:
		break;
	}
}

int bctl_dict_lookup(DBusMessageIter *dict, const char *name,
		     DBusMessageIter *value)
{
	DBusMessageIter entries;

	if (!dict || !name || dbus_message_iter_get_arg_type(dict) != DBUS_TYPE_ARRAY)
		return 0;
	dbus_message_iter_recurse(dict, &entries);
	while (dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry;
		const char *key = NULL;

		dbus_message_iter_recurse(&entries, &entry);
		if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING)
			dbus_message_iter_get_basic(&entry, &key);
		dbus_message_iter_next(&entry);
		if (!key) {
			dbus_message_iter_next(&entries);
			continue;
		}
		if (!strcmp(key, name) &&
		    dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
			/*
			 * 单次 recurse 直接定位到变体内容迭代器。
			 * 注意: 同一迭代器只能 recurse 一次(libdbus 会
			 * 断言失败), 不可先 recurse 再从子迭代器 recurse。
			 */
			if (value)
				dbus_message_iter_recurse(&entry, value);
			return 1;
		}
		dbus_message_iter_next(&entries);
	}
	return 0;
}

/* ---------------- 对象枚举 ---------------- */

int bctl_foreach_object(struct bluectl_ctx *c, bctl_object_cb cb, void *user)
{
	DBusMessage *reply;
	DBusMessageIter root, objs;

	if (!cb) {
		bctl_set_err(c, BLUECTL_ERR_INVALID_ARG, "object callback is required");
		return BLUECTL_ERR_INVALID_ARG;
	}
	reply = bctl_call(c, BLUEZ_ROOT_PATH, IFACE_OM, "GetManagedObjects", -1);
	if (!reply) {
		/* org.bluez 未运行时 D-Bus 报 UnknownMethod, 提示更友好一些 */
		if (strstr(c->err_name, "UnknownMethod") ||
		    strstr(c->err_name, "ServiceUnknown")) {
			c->err_code = BLUECTL_ERR_NO_BLUEZ;
			bctl_set_err(c, BLUECTL_ERR_NO_BLUEZ,
				     "org.bluez service not running (is bluetoothd up?)");
		}
		return c->err_code;
	}

	if (!dbus_message_iter_init(reply, &root) ||
	    dbus_message_iter_get_arg_type(&root) != DBUS_TYPE_ARRAY) {
		dbus_message_unref(reply);
		bctl_set_err(c, BLUECTL_ERR,
			     "GetManagedObjects: unexpected reply type");
		return BLUECTL_ERR;
	}

	dbus_message_iter_recurse(&root, &objs);
	while (dbus_message_iter_get_arg_type(&objs) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry;
		const char *path = NULL;

		dbus_message_iter_recurse(&objs, &entry);
		if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_OBJECT_PATH)
			dbus_message_iter_get_basic(&entry, &path);
		dbus_message_iter_next(&entry);

		if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_ARRAY) {
			DBusMessageIter ifaces;

			dbus_message_iter_recurse(&entry, &ifaces);
			while (dbus_message_iter_get_arg_type(&ifaces) ==
			       DBUS_TYPE_DICT_ENTRY) {
				DBusMessageIter ientry;
				const char *iface = NULL;

				dbus_message_iter_recurse(&ifaces, &ientry);
				if (dbus_message_iter_get_arg_type(&ientry) == DBUS_TYPE_STRING)
					dbus_message_iter_get_basic(&ientry, &iface);
				dbus_message_iter_next(&ientry);

				/*
				 * ientry 此刻位于接口的 a{sv} 属性数组。
				 * 注意: 必须把数组迭代器本身传给回调;
				 * 若先 recurse, 得到的是数组首个 dict entry,
				 * bctl_dict_lookup() 只认数组迭代器。
				 */
				if (iface && path &&
				    dbus_message_iter_get_arg_type(&ientry) == DBUS_TYPE_ARRAY)
					cb(path, iface, &ientry, user);
				dbus_message_iter_next(&ifaces);
			}
		}
		dbus_message_iter_next(&objs);
	}

	dbus_message_unref(reply);
	return BLUECTL_OK;
}

/* ---------------- 路径解析 ---------------- */

void bctl_mac_from_path(const char *path, char *out, size_t len)
{
	const char *p;
	size_t i;

	if (!out || !len)
		return;
	out[0] = '\0';
	if (!path)
		return;
	p = strrchr(path, '/');
	p = p ? p + 1 : path;
	if (strncmp(p, "dev_", 4))
		return;
	p += 4;
	/* 设备对象路径必须严格为 dev_XX_XX_XX_XX_XX_XX。 */
	if (strlen(p) != 17)
		return;
	for (i = 0; i < 17; i++) {
		if ((i % 3) == 2) {
			if (p[i] != '_')
				return;
		} else if (!isxdigit((unsigned char)p[i])) {
			return;
		}
	}
	/* "dev_AA_BB_CC_DD_EE_FF" -> "AA:BB:CC:DD:EE:FF" */
	for (i = 0; i < 17 && i < len - 1; i++)
		out[i] = (p[i] == '_') ? ':' : toupper((unsigned char)p[i]);
	out[i] = '\0';
}

struct bctl_find_first {
	const char *iface;
	char *out;
	size_t len;
	int found;
};

static void find_first_cb(const char *path, const char *iface,
			  DBusMessageIter *props, void *user)
{
	struct bctl_find_first *f = user;

	(void)props;
	if (strcmp(iface, f->iface))
		return;
	/* ObjectManager 字典顺序未定义，默认适配器选择保持稳定。 */
	if (f->found && strcmp(path, f->out) >= 0)
		return;
	bctl_strscpy(f->out, path, f->len);
	f->found = 1;
}

int bctl_adapter_path(struct bluectl_ctx *c, const char *adapter,
		      char *out, size_t len)
{
	struct bctl_find_first f = { IFACE_ADAPTER1, NULL, 0, 0 };
	int rv;

	if (!out || !len) {
		bctl_set_err(c, BLUECTL_ERR_INVALID_ARG, "invalid adapter output buffer");
		return BLUECTL_ERR_INVALID_ARG;
	}
	out[0] = '\0';

	if (!adapter || !adapter[0]) {
		f.out = out;
		f.len = len;
		rv = bctl_foreach_object(c, find_first_cb, &f);
		if (rv < 0)
			return rv;
		if (!f.found) {
			bctl_set_err(c, BLUECTL_ERR_NO_ADAPTER,
				     "no bluetooth adapter found");
			return BLUECTL_ERR_NO_ADAPTER;
		}
		return BLUECTL_OK;
	}
	if (adapter[0] == '/') {
		if (strlen(adapter) >= len) {
			bctl_set_err(c, BLUECTL_ERR_INVALID_ARG,
				     "adapter path is too long");
			return BLUECTL_ERR_INVALID_ARG;
		}
		bctl_strscpy(out, adapter, len);
		return BLUECTL_OK;
	}
	if (snprintf(out, len, "%s/%s", BLUEZ_ROOT_PATH, adapter) >= (int)len) {
		out[0] = '\0';
		bctl_set_err(c, BLUECTL_ERR_INVALID_ARG, "adapter name is too long");
		return BLUECTL_ERR_INVALID_ARG;
	}
	return BLUECTL_OK;
}

struct bctl_find_device {
	const char *target;
	char *out;
	size_t len;
	int found;
};

static void find_device_cb(const char *path, const char *iface,
			   DBusMessageIter *props, void *user)
{
	struct bctl_find_device *f = user;
	char addr[BLUECTL_ADDR_LEN];
	DBusMessageIter it;

	if (f->found || strcmp(iface, IFACE_DEVICE1))
		return;
	/* 对象路径自带 MAC, 免解析属性 */
	bctl_mac_from_path(path, addr, sizeof(addr));
	if (addr[0] && !strcasecmp(addr, f->target))
		goto hit;
	if (bctl_dict_lookup(props, "Address", &it)) {
		bctl_read_str(&it, addr, sizeof(addr));
		if (addr[0] && !strcasecmp(addr, f->target))
			goto hit;
	}
	return;
hit:
	bctl_strscpy(f->out, path, f->len);
	f->found = 1;
}

int bctl_device_path(struct bluectl_ctx *c, const char *device,
		     char *out, size_t len)
{
	struct bctl_find_device f = { NULL, NULL, 0, 0 };
	int rv;

	if (!out || !len) {
		bctl_set_err(c, BLUECTL_ERR_INVALID_ARG, "invalid device output buffer");
		return BLUECTL_ERR_INVALID_ARG;
	}
	out[0] = '\0';
	if (!device || !device[0]) {
		bctl_set_err(c, BLUECTL_ERR_INVALID_ARG, "device is required");
		return BLUECTL_ERR_INVALID_ARG;
	}
	if (device[0] == '/') {
		if (strlen(device) >= len) {
			bctl_set_err(c, BLUECTL_ERR_INVALID_ARG,
				     "device path is too long");
			return BLUECTL_ERR_INVALID_ARG;
		}
		bctl_strscpy(out, device, len);
		return BLUECTL_OK;
	}
	f.target = device;
	f.out = out;
	f.len = len;
	rv = bctl_foreach_object(c, find_device_cb, &f);
	if (rv < 0)
		return rv;
	if (!f.found) {
		bctl_set_err(c, BLUECTL_ERR_NOT_FOUND,
			     "device '%s' not found", device);
		return BLUECTL_ERR_NOT_FOUND;
	}
	return BLUECTL_OK;
}

/* ---------------- 属性填充 ---------------- */

void bctl_adapter_fill(DBusMessageIter *props, bluectl_adapter_t *out)
{
	DBusMessageIter it;

	memset(out, 0, sizeof(*out));
	if (!props)
		return;
	if (bctl_dict_lookup(props, "Address", &it))
		bctl_read_str(&it, out->address, sizeof(out->address));
	if (bctl_dict_lookup(props, "Name", &it))
		bctl_read_str(&it, out->name, sizeof(out->name));
	if (bctl_dict_lookup(props, "Alias", &it))
		bctl_read_str(&it, out->alias, sizeof(out->alias));
	if (bctl_dict_lookup(props, "Powered", &it))
		out->powered = bctl_read_bool(&it, 0);
	if (bctl_dict_lookup(props, "Discoverable", &it))
		out->discoverable = bctl_read_bool(&it, 0);
	if (bctl_dict_lookup(props, "Discovering", &it))
		out->discovering = bctl_read_bool(&it, 0);
	if (bctl_dict_lookup(props, "Pairable", &it))
		out->pairable = bctl_read_bool(&it, 0);
	if (bctl_dict_lookup(props, "DiscoverableTimeout", &it))
		out->discoverable_timeout = (unsigned int)bctl_read_num(&it, 0);
	if (bctl_dict_lookup(props, "PairableTimeout", &it))
		out->pairable_timeout = (unsigned int)bctl_read_num(&it, 0);
}

void bctl_device_fill(DBusMessageIter *props, bluectl_device_t *out)
{
	DBusMessageIter it;
	int stored = 0;

	memset(out, 0, sizeof(*out));
	if (!props)
		return;
	if (bctl_dict_lookup(props, "Address", &it))
		bctl_read_str(&it, out->address, sizeof(out->address));
	if (bctl_dict_lookup(props, "Alias", &it))
		bctl_read_str(&it, out->name, sizeof(out->name));
	if (!out->name[0] && bctl_dict_lookup(props, "Name", &it))
		bctl_read_str(&it, out->name, sizeof(out->name));
	if (bctl_dict_lookup(props, "Icon", &it))
		bctl_read_str(&it, out->icon, sizeof(out->icon));
	if (bctl_dict_lookup(props, "Paired", &it))
		out->paired = bctl_read_bool(&it, 0);
	if (bctl_dict_lookup(props, "Connected", &it))
		out->connected = bctl_read_bool(&it, 0);
	if (bctl_dict_lookup(props, "Trusted", &it))
		out->trusted = bctl_read_bool(&it, 0);
	if (bctl_dict_lookup(props, "Blocked", &it))
		out->blocked = bctl_read_bool(&it, 0);
	if (bctl_dict_lookup(props, "RSSI", &it))
		out->rssi = (int)bctl_read_num(&it, 0);
	if (bctl_dict_lookup(props, "Class", &it))
		out->dev_class = (unsigned int)bctl_read_num(&it, 0);

	if (bctl_dict_lookup(props, "UUIDs", &it) &&
	    dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
		DBusMessageIter sub;

		dbus_message_iter_recurse(&it, &sub);
		while (dbus_message_iter_get_arg_type(&sub) == DBUS_TYPE_STRING) {
			const char *u = NULL;

			dbus_message_iter_get_basic(&sub, &u);
			if (u) {
				out->uuid_count++;
				if (stored < BLUECTL_MAX_UUIDS)
					bctl_strscpy(out->uuids[stored++], u,
						     BLUECTL_UUID_LEN);
			}
			dbus_message_iter_next(&sub);
		}
	}
}

/* ---------------- 事件 ---------------- */

static void bctl_emit(struct bluectl_ctx *c, bluectl_event_type_t type,
		      const char *path, const char *iface,
		      const char *property, const char *value)
{
	bluectl_event_t ev;

	if (!c->ev_cb)
		return;
	memset(&ev, 0, sizeof(ev));
	ev.type = type;
	ev.path = path;
	ev.interface = iface;
	ev.property = property;
	ev.value = value;
	c->ev_cb(&ev, c->ev_user);
}

static void bctl_variant_str(DBusMessageIter *var, char *out, size_t len)
{
	if (!var) {
		bctl_strscpy(out, "", len);
		return;
	}
	switch (dbus_message_iter_get_arg_type(var)) {
	case DBUS_TYPE_BOOLEAN: {
		dbus_bool_t v = FALSE;

		dbus_message_iter_get_basic(var, &v);
		bctl_strscpy(out, v ? "true" : "false", len);
		break;
	}
	case DBUS_TYPE_STRING:
	case DBUS_TYPE_OBJECT_PATH:
	case DBUS_TYPE_SIGNATURE: {
		const char *s = NULL;

		dbus_message_iter_get_basic(var, &s);
		bctl_strscpy(out, s ? s : "", len);
		break;
	}
	case DBUS_TYPE_BYTE:
	case DBUS_TYPE_INT16:
	case DBUS_TYPE_UINT16:
	case DBUS_TYPE_INT32:
	case DBUS_TYPE_UINT32:
	case DBUS_TYPE_INT64:
	case DBUS_TYPE_UINT64:
		snprintf(out, len, "%ld", bctl_read_num(var, 0));
		break;
	case DBUS_TYPE_ARRAY:
		bctl_strscpy(out, "[...]", len);
		break;
	default:
		bctl_strscpy(out, "?", len);
		break;
	}
}

static void bctl_iface_events(const char *iface,
			      bluectl_event_type_t *added,
			      bluectl_event_type_t *removed)
{
	if (!strcmp(iface, IFACE_ADAPTER1)) {
		*added = BLUECTL_EV_ADAPTER_ADDED;
		*removed = BLUECTL_EV_ADAPTER_REMOVED;
	} else if (!strcmp(iface, IFACE_DEVICE1)) {
		*added = BLUECTL_EV_DEVICE_ADDED;
		*removed = BLUECTL_EV_DEVICE_REMOVED;
	} else {
		*added = BLUECTL_EV_INTERFACES_ADDED;
		*removed = BLUECTL_EV_INTERFACES_REMOVED;
	}
}

static void handle_interfaces_added(struct bluectl_ctx *c, DBusMessage *msg)
{
	DBusMessageIter it, ifaces;
	const char *path = NULL;

	if (!dbus_message_iter_init(msg, &it) ||
	    dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_OBJECT_PATH)
		return;
	dbus_message_iter_get_basic(&it, &path);
	dbus_message_iter_next(&it);
	if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY)
		return;
	dbus_message_iter_recurse(&it, &ifaces);
	while (dbus_message_iter_get_arg_type(&ifaces) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry;
		const char *name = NULL;
		bluectl_event_type_t added, removed;

		dbus_message_iter_recurse(&ifaces, &entry);
		if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING)
			dbus_message_iter_get_basic(&entry, &name);
		if (name) {
			bctl_iface_events(name, &added, &removed);
			bctl_emit(c, added, path, name, NULL, NULL);
		}
		dbus_message_iter_next(&ifaces);
	}
}

static void handle_interfaces_removed(struct bluectl_ctx *c, DBusMessage *msg)
{
	DBusMessageIter it, names;
	const char *path = NULL;

	if (!dbus_message_iter_init(msg, &it) ||
	    dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_OBJECT_PATH)
		return;
	dbus_message_iter_get_basic(&it, &path);
	dbus_message_iter_next(&it);
	if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY)
		return;
	dbus_message_iter_recurse(&it, &names);
	while (dbus_message_iter_get_arg_type(&names) == DBUS_TYPE_STRING) {
		const char *name = NULL;
		bluectl_event_type_t added, removed;

		dbus_message_iter_get_basic(&names, &name);
		if (name) {
			bctl_iface_events(name, &added, &removed);
			bctl_emit(c, removed, path, name, NULL, NULL);
		}
		dbus_message_iter_next(&names);
	}
}

static void handle_properties_changed(struct bluectl_ctx *c, DBusMessage *msg)
{
	DBusMessageIter it, changed, entries;
	const char *iface = NULL;
	const char *path = dbus_message_get_path(msg);

	if (!dbus_message_iter_init(msg, &it) ||
	    dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING)
		return;
	dbus_message_iter_get_basic(&it, &iface);
	dbus_message_iter_next(&it);
	if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY)
		return;

	/* 变化的属性: 携带值 */
	dbus_message_iter_recurse(&it, &changed);
	while (dbus_message_iter_get_arg_type(&changed) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry, val_it;
		const char *key = NULL;
		char val[64];

		dbus_message_iter_recurse(&changed, &entry);
		if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING)
			dbus_message_iter_get_basic(&entry, &key);
		dbus_message_iter_next(&entry);
		if (key &&
		    dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
			/* 单次 recurse: 直接定位到变体内容 */
			dbus_message_iter_recurse(&entry, &val_it);
			bctl_variant_str(&val_it, val, sizeof(val));
			bctl_emit(c, BLUECTL_EV_PROPERTY_CHANGED, path, iface,
				  key, val);
		}
		dbus_message_iter_next(&changed);
	}

	/* 失效的属性: 只有名字 */
	if (!dbus_message_iter_next(&it) ||
	    dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY)
		return;
	dbus_message_iter_recurse(&it, &entries);
	while (dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_STRING) {
		const char *key = NULL;

		dbus_message_iter_get_basic(&entries, &key);
		if (key)
			bctl_emit(c, BLUECTL_EV_PROPERTY_CHANGED, path, iface,
				  key, NULL);
		dbus_message_iter_next(&entries);
	}
}

static void handle_name_owner_changed(struct bluectl_ctx *c, DBusMessage *msg)
{
	DBusMessageIter it;
	const char *name = NULL;
	const char *old_owner = NULL;
	const char *new_owner = NULL;

	if (!dbus_message_iter_init(msg, &it))
		return;
	if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING)
		return;
	dbus_message_iter_get_basic(&it, &name);
	if (!name || strcmp(name, BLUEZ_NAME))
		return;
	if (!dbus_message_iter_next(&it) ||
	    dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING)
		return;
	dbus_message_iter_get_basic(&it, &old_owner);
	if (!dbus_message_iter_next(&it) ||
	    dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING)
		return;
	dbus_message_iter_get_basic(&it, &new_owner);
	if (new_owner && new_owner[0])
		bctl_emit(c, BLUECTL_EV_BLUEZ_UP, NULL, BLUEZ_NAME, NULL, NULL);
	else
		bctl_emit(c, BLUECTL_EV_BLUEZ_DOWN, NULL, BLUEZ_NAME, NULL, NULL);
}

/*
 * 消息过滤器: 只处理信号; 返回 NOT_YET_HANDLED 以保证
 * agent 对象路径注册的 vtable 仍能收到方法调用。
 */
static DBusHandlerResult bctl_filter(DBusConnection *conn, DBusMessage *msg,
				     void *user)
{
	struct bluectl_ctx *c = user;
	const char *iface = dbus_message_get_interface(msg);
	const char *member = dbus_message_get_member(msg);

	(void)conn;
	if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_SIGNAL)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	if (!iface || !member)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	if (!strcmp(iface, IFACE_OM)) {
		if (!strcmp(member, "InterfacesAdded"))
			handle_interfaces_added(c, msg);
		else if (!strcmp(member, "InterfacesRemoved"))
			handle_interfaces_removed(c, msg);
	} else if (!strcmp(iface, IFACE_PROPS)) {
		if (!strcmp(member, "PropertiesChanged"))
			handle_properties_changed(c, msg);
	} else if (!strcmp(iface, "org.freedesktop.DBus")) {
		if (!strcmp(member, "NameOwnerChanged"))
			handle_name_owner_changed(c, msg);
	}
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ---------------- 公共 API ---------------- */

int bluectl_init(void)
{
	struct bluectl_ctx *c = &g_bluectl;
	DBusError err;
	int i;

	if (c->inited)
		return BLUECTL_OK;

	dbus_threads_init_default();
	dbus_error_init(&err);
	c->conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
	if (!c->conn) {
		int rv = bctl_set_dbus_err(c, &err);

		dbus_error_free(&err);
		return rv;
	}
	dbus_connection_set_exit_on_disconnect(c->conn, FALSE);

	if (!dbus_connection_add_filter(c->conn, bctl_filter, c, NULL)) {
		dbus_connection_unref(c->conn);
		c->conn = NULL;
		bctl_set_err(c, BLUECTL_ERR_NO_MEM, "add filter failed");
		return BLUECTL_ERR_NO_MEM;
	}

	dbus_error_init(&err);
	for (i = 0; i < BCTL_N_MATCH_RULES; i++)
		dbus_bus_add_match(c->conn, bctl_match_rules[i], &err);
	if (dbus_error_is_set(&err)) {
		int rv = bctl_set_dbus_err(c, &err);

		dbus_error_free(&err);
		bluectl_deinit();
		return rv;
	}

	/* 保留 bluectl_init() 之前通过 bluectl_set_timeout() 设置的值。 */
	if (!c->timeout_ms)
		c->timeout_ms = BLUECTL_DEFAULT_TIMEOUT_MS;
	c->err_code = BLUECTL_OK;
	c->err[0] = '\0';
	c->inited = 1;
	return BLUECTL_OK;
}

void bluectl_deinit(void)
{
	struct bluectl_ctx *c = &g_bluectl;
	int i;

	if (c->conn) {
#ifdef CONFIG_BLUECTL_AGENT_ENABLED
		bluectl_agent_unregister();
#endif
		for (i = 0; i < BCTL_N_MATCH_RULES; i++)
			dbus_bus_remove_match(c->conn, bctl_match_rules[i], NULL);
		dbus_connection_remove_filter(c->conn, bctl_filter, c);
		dbus_connection_unref(c->conn);
		c->conn = NULL;
	}
	c->inited = 0;
	c->ev_cb = NULL;
	c->ev_user = NULL;
	c->err_code = BLUECTL_OK;
	c->err[0] = '\0';
}

int bluectl_is_connected(void)
{
	struct bluectl_ctx *c = &g_bluectl;

	return (c->conn && dbus_connection_get_is_connected(c->conn)) ? 1 : 0;
}

const char *bluectl_last_error(void)
{
	return g_bluectl.err;
}

const char *bluectl_version(void)
{
	return BLUECTL_VERSION;
}

void bluectl_set_timeout(unsigned int timeout_ms)
{
	if (!timeout_ms)
		timeout_ms = BLUECTL_DEFAULT_TIMEOUT_MS;
	else if (timeout_ms > (unsigned int)INT_MAX)
		timeout_ms = INT_MAX;
	g_bluectl.timeout_ms = timeout_ms;
}

int bluectl_set_event_callback(bluectl_event_cb_t cb, void *user_data)
{
	struct bluectl_ctx *c = &g_bluectl;

	if (!bctl_check_conn(c))
		return BLUECTL_ERR_NO_CONN;
	c->ev_cb = cb;
	c->ev_user = user_data;
	return BLUECTL_OK;
}

/* 分发已入队消息, 返回分发数量, 内存不足时返回 -1 */
static int bctl_drain(struct bluectl_ctx *c)
{
	int dispatched = 0;
	int i;

	for (i = 0; i < 64; i++) {
		if (dbus_connection_get_dispatch_status(c->conn) ==
		    DBUS_DISPATCH_COMPLETE)
			break;
		if (dbus_connection_dispatch(c->conn) == DBUS_DISPATCH_NEED_MEMORY)
			return -1;
		dispatched++;
	}
	return dispatched;
}

int bluectl_process(unsigned int timeout_ms)
{
	struct bluectl_ctx *c = &g_bluectl;
	int dispatched;

	if (!bctl_check_conn(c))
		return BLUECTL_ERR_NO_CONN;

	dispatched = bctl_drain(c);
	if (dispatched < 0) {
		bctl_set_err(c, BLUECTL_ERR_NO_MEM, "dispatch out of memory");
		return BLUECTL_ERR_NO_MEM;
	}

	if (dispatched == 0) {
		/* 队列空: 阻塞等待新消息到达(0 = 非阻塞轮询) */
		/* 超大 unsigned 值转换为 int 会变负并导致无限等待。 */
		dbus_connection_read_write(c->conn,
				timeout_ms > (unsigned int)INT_MAX ? INT_MAX :
				(int)timeout_ms);
		dispatched = bctl_drain(c);
		if (dispatched < 0) {
			bctl_set_err(c, BLUECTL_ERR_NO_MEM, "dispatch out of memory");
			return BLUECTL_ERR_NO_MEM;
		}
	}

	if (!dbus_connection_get_is_connected(c->conn)) {
		bctl_set_err(c, BLUECTL_ERR_NO_CONN, "dbus connection lost");
		return BLUECTL_ERR_NO_CONN;
	}
	return dispatched;
}
