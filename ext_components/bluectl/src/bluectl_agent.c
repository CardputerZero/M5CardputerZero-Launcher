/*
 * bluectl_agent.c - org.bluez.Agent1 配对代理实现
 *
 * 依赖 bluectl_process() 驱动消息分发: BlueZ 的配对请求
 * (RequestPinCode/RequestConfirmation/...) 会以方法调用形式到达,
 * 由 vtable 转成回调交给应用, 应用再通过 bluectl_agent_reply() 应答。
 */
#include "bluectl_internal.h"

#include <stdio.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>

/* bluectl_agent_reply() 可由应用工作线程调用，保护挂起请求链表。 */
static volatile int g_pending_lock;

static void pending_lock(void)
{
	while (__sync_lock_test_and_set(&g_pending_lock, 1))
		sched_yield();
}

static void pending_unlock(void)
{
	__sync_lock_release(&g_pending_lock);
}

/* needs_reply=1 的方法集合 */
static int method_needs_reply(const char *method)
{
	return !strcmp(method, "RequestPinCode") ||
	       !strcmp(method, "RequestPasskey") ||
	       !strcmp(method, "RequestConfirmation") ||
	       !strcmp(method, "RequestAuthorization") ||
	       !strcmp(method, "AuthorizeService");
}

static void agent_notify(struct bluectl_ctx *c, bluectl_agent_request_t *req)
{
	if (c->agent_cb)
		c->agent_cb(req, c->agent_user);
}

static int send_reply(struct bluectl_ctx *c, DBusMessage *msg, int accept,
		      const char *method, const char *code)
{
	DBusMessage *reply;

	if (!accept) {
		reply = dbus_message_new_error(msg, "org.bluez.Error.Rejected",
					       "rejected by application");
	} else if (method && !strcmp(method, "RequestPinCode")) {
		const char *pin = (code && code[0]) ? code : "0000";
		DBusMessageIter it;

		reply = dbus_message_new_method_return(msg);
		if (reply) {
			dbus_message_iter_init_append(reply, &it);
			if (!dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &pin)) {
				dbus_message_unref(reply);
				reply = NULL;
			}
		}
	} else if (method && !strcmp(method, "RequestPasskey")) {
		dbus_uint32_t passkey = (dbus_uint32_t)strtoul(
			(code && code[0]) ? code : "0", NULL, 10);
		DBusMessageIter it;

		reply = dbus_message_new_method_return(msg);
		if (reply) {
			dbus_message_iter_init_append(reply, &it);
			if (!dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &passkey)) {
				dbus_message_unref(reply);
				reply = NULL;
			}
		}
	} else {
		reply = dbus_message_new_method_return(msg);
	}

	if (!reply) {
		bctl_set_err(c, BLUECTL_ERR_NO_MEM, "no memory for agent reply");
		return BLUECTL_ERR_NO_MEM;
	}
	if (!dbus_connection_send(c->conn, reply, NULL)) {
		dbus_message_unref(reply);
		bctl_set_err(c, BLUECTL_ERR_NO_MEM, "send agent reply failed");
		return BLUECTL_ERR_NO_MEM;
	}
	/*
	 * 这里不能 dbus_connection_flush(): 本函数可能在
	 * bluectl_process() 的消息分发(handler)上下文中被调用,
	 * 若另一线程正阻塞在 send_with_reply_and_block(),
	 * flush 会与 io 路径锁互等造成死锁。
	 * send 只入队, 出站数据由下一次 bluectl_process() 的
	 * read_write(DO_WRITING) 写出。
	 */
	dbus_message_unref(reply);
	return BLUECTL_OK;
}

/* 向所有挂起请求回 Canceled 并清空(BlueZ 发来 Cancel/Release 或应用主动取消) */
static void agent_cancel_all(struct bluectl_ctx *c, const char *reason)
{
	struct bctl_pending_agent *p, *next;

	pending_lock();
	p = c->pending_agents;
	c->pending_agents = NULL;
	pending_unlock();
	while (p) {
		next = p->next;
		if (p->msg && c->conn) {
			DBusMessage *err = dbus_message_new_error(
				p->msg, "org.bluez.Error.Canceled", reason);

			if (err) {
				/* 只入队不 flush(handler 上下文, 见 send_reply 注释) */
				dbus_connection_send(c->conn, err, NULL);
				dbus_message_unref(err);
			}
		}
		if (p->msg)
			dbus_message_unref(p->msg);
		free(p);
		p = next;
	}
}

static DBusHandlerResult handle_agent_call(struct bluectl_ctx *c,
					   DBusMessage *msg, const char *method)
{
	const char *device_path = NULL;
	DBusMessageIter it;
	bluectl_agent_request_t req;

	/* RequestConfirmation 需要 passkey, AuthorizeService 需要 uuid */
	if (dbus_message_iter_init(msg, &it) &&
	    dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_OBJECT_PATH)
		dbus_message_iter_get_basic(&it, &device_path);

	memset(&req, 0, sizeof(req));
	req.method = method;
	if (device_path) {
		bctl_strscpy(req.path, device_path, sizeof(req.path));
		bctl_mac_from_path(device_path, req.device, sizeof(req.device));
	}

	if (!strcmp(method, "Release")) {
		agent_cancel_all(c, "agent released");
		goto reply_now;
	}
	if (!strcmp(method, "Cancel")) {
		agent_cancel_all(c, "pairing canceled");
		goto reply_now;
	}
	if (!strcmp(method, "DisplayPinCode")) {
		/* 纯展示请求: 立即应答并通知应用 */
		req.needs_reply = 0;
		agent_notify(c, &req);
		goto reply_now;
	}
	if (!strcmp(method, "DisplayPasskey")) {
		dbus_uint32_t passkey = 0;

		if (dbus_message_iter_init(msg, &it) &&
		    dbus_message_iter_next(&it) &&
		    dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_UINT32) {
			dbus_message_iter_get_basic(&it, &passkey);
			snprintf(req.passkey, sizeof(req.passkey), "%u", passkey);
		}
		req.needs_reply = 0;
		agent_notify(c, &req);
		goto reply_now;
	}

	if (!method_needs_reply(method)) {
		DBusMessage *err = dbus_message_new_error(
			msg, "org.freedesktop.DBus.Error.UnknownMethod",
			"unknown agent method");

		if (err) {
			/* 只入队不 flush(handler 上下文, 见 send_reply 注释) */
			dbus_connection_send(c->conn, err, NULL);
			dbus_message_unref(err);
		}
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	if (!strcmp(method, "RequestConfirmation")) {
		dbus_uint32_t passkey = 0;

		if (dbus_message_iter_init(msg, &it) &&
		    dbus_message_iter_next(&it) &&
		    dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_UINT32) {
			dbus_message_iter_get_basic(&it, &passkey);
			snprintf(req.passkey, sizeof(req.passkey), "%u", passkey);
		}
	} else if (!strcmp(method, "AuthorizeService")) {
		const char *uuid = NULL;

		if (dbus_message_iter_init(msg, &it) &&
		    dbus_message_iter_next(&it) &&
		    dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING) {
			dbus_message_iter_get_basic(&it, &uuid);
			if (uuid)
				bctl_strscpy(req.uuid, uuid, sizeof(req.uuid));
		}
	}

	/* 挂起请求, 等待应用 bluectl_agent_reply() */
	{
		struct bctl_pending_agent *p;

		p = calloc(1, sizeof(*p));
		if (!p)
			goto oom;
		pending_lock();
		req.id = c->agent_next_id++;
		p->id = req.id;
		bctl_strscpy(p->method, method, sizeof(p->method));
		p->msg = dbus_message_ref(msg);
		p->next = c->pending_agents;
		c->pending_agents = p;
		pending_unlock();

		req.needs_reply = 1;
		if (!c->agent_cb) {
			/* 无人处理: 直接拒绝, 避免配对挂死 */
			bluectl_agent_reply(req.id, 0, NULL);
			return DBUS_HANDLER_RESULT_HANDLED;
		}
		agent_notify(c, &req);
	}
	return DBUS_HANDLER_RESULT_HANDLED;

reply_now:
	{
		DBusMessage *reply = dbus_message_new_method_return(msg);

		if (reply) {
			/* 只入队不 flush(handler 上下文, 见 send_reply 注释) */
			dbus_connection_send(c->conn, reply, NULL);
			dbus_message_unref(reply);
		}
	}
	return DBUS_HANDLER_RESULT_HANDLED;
oom:
	{
		DBusMessage *err = dbus_message_new_error(
			msg, "org.freedesktop.DBus.Error.NoReply", "out of memory");

		if (err) {
			dbus_connection_send(c->conn, err, NULL);
			dbus_message_unref(err);
		}
	}
	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult agent_message(DBusConnection *conn, DBusMessage *msg,
				       void *user)
{
	struct bluectl_ctx *c = user;
	const char *iface = dbus_message_get_interface(msg);
	const char *method = dbus_message_get_member(msg);

	(void)conn;
	if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL ||
	    !iface || !method || strcmp(iface, IFACE_AGENT1))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	return handle_agent_call(c, msg, method);
}

static void agent_unregistered(DBusConnection *conn, void *user)
{
	(void)conn;
	(void)user;
}

static const DBusObjectPathVTable agent_vtable = {
	.unregister_function = agent_unregistered,
	.message_function = agent_message,
};

/* ---------------- 公共 API ---------------- */

int bluectl_agent_set_callback(bluectl_agent_cb_t cb, void *user_data)
{
	struct bluectl_ctx *c = &g_bluectl;

	if (!bctl_check_conn(c))
		return BLUECTL_ERR_NO_CONN;
	c->agent_cb = cb;
	c->agent_user = user_data;
	return BLUECTL_OK;
}

int bluectl_agent_register(const char *capability)
{
	struct bluectl_ctx *c = &g_bluectl;
	DBusError err;
	const char *cap = (capability && capability[0]) ?
			  capability : BLUECTL_CAP_KEYBOARD_DISPLAY;
	const char *agent_path = BLUECTL_AGENT_PATH;
	DBusMessage *call;
	DBusMessageIter it;
	DBusMessage *reply;

	if (!bctl_check_conn(c))
		return BLUECTL_ERR_NO_CONN;
	if (c->agent_registered)
		return BLUECTL_OK;

	dbus_error_init(&err);
	if (!dbus_connection_try_register_object_path(c->conn, BLUECTL_AGENT_PATH,
						      &agent_vtable, c, &err)) {
		bctl_set_dbus_err(c, &err);
		dbus_error_free(&err);
		return c->err_code;
	}

	call = dbus_message_new_method_call(BLUEZ_NAME, BLUEZ_ROOT_PATH,
					    IFACE_AGENT_MANAGER1, "RegisterAgent");
	if (!call) {
		dbus_connection_unregister_object_path(c->conn, BLUECTL_AGENT_PATH);
		bctl_set_err(c, BLUECTL_ERR_NO_MEM, "no memory for message");
		return BLUECTL_ERR_NO_MEM;
	}
	dbus_message_iter_init_append(call, &it);
	if (!dbus_message_iter_append_basic(&it, DBUS_TYPE_OBJECT_PATH,
					    &agent_path) ||
	    !dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &cap)) {
		dbus_message_unref(call);
		dbus_connection_unregister_object_path(c->conn, BLUECTL_AGENT_PATH);
		bctl_set_err(c, BLUECTL_ERR_NO_MEM, "message build failed");
		return BLUECTL_ERR_NO_MEM;
	}
	reply = bctl_send(c, call, -1);
	if (!reply) {
		int rv = c->err_code;

		dbus_connection_unregister_object_path(c->conn, BLUECTL_AGENT_PATH);
		return rv;
	}
	dbus_message_unref(reply);
	c->agent_registered = 1;
	return BLUECTL_OK;
}

int bluectl_agent_request_default(void)
{
	struct bluectl_ctx *c = &g_bluectl;
	DBusMessage *call;
	DBusMessageIter it;
	DBusMessage *reply;

	if (!bctl_check_conn(c))
		return BLUECTL_ERR_NO_CONN;
	if (!c->agent_registered) {
		bctl_set_err(c, BLUECTL_ERR_INVALID_ARG,
			     "agent not registered");
		return BLUECTL_ERR_INVALID_ARG;
	}
	call = dbus_message_new_method_call(BLUEZ_NAME, BLUEZ_ROOT_PATH,
					    IFACE_AGENT_MANAGER1,
					    "RequestDefaultAgent");
	if (!call) {
		bctl_set_err(c, BLUECTL_ERR_NO_MEM, "no memory for message");
		return BLUECTL_ERR_NO_MEM;
	}
	dbus_message_iter_init_append(call, &it);
	{
		const char *agent_path = BLUECTL_AGENT_PATH;

		if (!dbus_message_iter_append_basic(&it, DBUS_TYPE_OBJECT_PATH,
						    &agent_path)) {
			dbus_message_unref(call);
			bctl_set_err(c, BLUECTL_ERR_NO_MEM, "message build failed");
			return BLUECTL_ERR_NO_MEM;
		}
	}
	reply = bctl_send(c, call, -1);
	if (!reply)
		return c->err_code;
	dbus_message_unref(reply);
	return BLUECTL_OK;
}

int bluectl_agent_unregister(void)
{
	struct bluectl_ctx *c = &g_bluectl;

	if (!c->conn)
		return BLUECTL_OK;
	agent_cancel_all(c, "agent stopped");
	if (c->agent_registered) {
		DBusMessage *call;
		DBusMessageIter it;
		const char *agent_path = BLUECTL_AGENT_PATH;

		call = dbus_message_new_method_call(BLUEZ_NAME, BLUEZ_ROOT_PATH,
						    IFACE_AGENT_MANAGER1,
						    "UnregisterAgent");
		if (call) {
			dbus_message_iter_init_append(call, &it);
			if (dbus_message_iter_append_basic(&it,
					DBUS_TYPE_OBJECT_PATH,
					&agent_path))
				bctl_send(c, call, -1);
			else
				dbus_message_unref(call);
		}
		dbus_connection_unregister_object_path(c->conn, BLUECTL_AGENT_PATH);
		c->agent_registered = 0;
	}
	c->agent_cb = NULL;
	c->agent_user = NULL;
	return BLUECTL_OK;
}

int bluectl_agent_reply(unsigned long id, int accept, const char *code)
{
	struct bluectl_ctx *c = &g_bluectl;
	struct bctl_pending_agent **link;
	struct bctl_pending_agent *p;

	if (!bctl_check_conn(c))
		return BLUECTL_ERR_NO_CONN;

	pending_lock();
	link = &c->pending_agents;
	while (*link && (*link)->id != id)
		link = &(*link)->next;
	p = *link;
	if (!p) {
		pending_unlock();
		bctl_set_err(c, BLUECTL_ERR_NOT_FOUND,
			     "agent request %lu not pending", id);
		return BLUECTL_ERR_NOT_FOUND;
	}
	*link = p->next;
	pending_unlock();
	{
		int rv = send_reply(c, p->msg, accept, p->method, code);

		dbus_message_unref(p->msg);
		free(p);
		if (rv < 0)
			return rv;
	}
	return BLUECTL_OK;
}

int bluectl_agent_cancel_pending(void)
{
	struct bluectl_ctx *c = &g_bluectl;

	if (!bctl_check_conn(c))
		return BLUECTL_ERR_NO_CONN;
	agent_cancel_all(c, "canceled by application");
	return BLUECTL_OK;
}
