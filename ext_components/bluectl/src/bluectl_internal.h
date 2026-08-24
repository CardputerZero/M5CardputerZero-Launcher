/*
 * bluectl_internal.h - 内部共享声明, 仅本组件源文件使用
 */
#ifndef _BLUECTL_INTERNAL_H_
#define _BLUECTL_INTERNAL_H_

#include "bluectl.h"
#include <dbus/dbus.h>

#define BLUEZ_NAME             "org.bluez"
#define BLUEZ_ROOT_PATH        "/org/bluez"
#define IFACE_OM               "org.freedesktop.DBus.ObjectManager"
#define IFACE_PROPS            "org.freedesktop.DBus.Properties"
#define IFACE_ADAPTER1         "org.bluez.Adapter1"
#define IFACE_DEVICE1          "org.bluez.Device1"
#define IFACE_AGENT1           "org.bluez.Agent1"
#define IFACE_AGENT_MANAGER1   "org.bluez.AgentManager1"
#define IFACE_MEDIA_PLAYER1    "org.bluez.MediaPlayer1"

#ifdef CONFIG_BLUECTL_AGENT_ENABLED
struct bctl_pending_agent {
	unsigned long id;
	char method[24];
	DBusMessage *msg;	/* 持有引用, 应答后释放 */
	struct bctl_pending_agent *next;
};
#endif

struct bluectl_ctx {
	DBusConnection *conn;
	int inited;
	unsigned int timeout_ms;
	int err_code;
	char err[192];
	char err_name[96];	/* DBus 错误名, 如 org.bluez.Error.InProgress */

	bluectl_event_cb_t ev_cb;
	void *ev_user;

#ifdef CONFIG_BLUECTL_AGENT_ENABLED
	int agent_registered;
	unsigned long agent_next_id;
	struct bctl_pending_agent *pending_agents;
	bluectl_agent_cb_t agent_cb;
	void *agent_user;
#endif
};

/* 全局单例上下文 */
extern struct bluectl_ctx g_bluectl;

/* ---------------- 基础工具 ---------------- */

/* 记录错误(带错误码), fmt 为 printf 风格 */
void bctl_set_err(struct bluectl_ctx *c, int code, const char *fmt, ...);
/* 从 DBusError 记录错误并返回映射后的错误码 */
int bctl_set_dbus_err(struct bluectl_ctx *c, const DBusError *err);
/* 便捷检查 */
int bctl_check_conn(struct bluectl_ctx *c);

/* 安全拷贝, 保证 NUL 结尾与截断安全 */
void bctl_strscpy(char *dst, const char *src, size_t len);

/*
 * 发送方法调用并阻塞等待应答。
 * call 由调用者构造(含参数), 本函数发送后 unref(call)。
 * 成功返回应答(调用者用毕 dbus_message_unref), 失败返回 NULL 且已记录错误。
 */
DBusMessage *bctl_send(struct bluectl_ctx *c, DBusMessage *call, int timeout_ms);

/* 无参数方法调用的便捷封装 */
DBusMessage *bctl_call(struct bluectl_ctx *c, const char *path,
		       const char *iface, const char *method, int timeout_ms);

/*
 * Properties.Set(path, iface, name, value)。
 * type 取 DBUS_TYPE_BOOLEAN / DBUS_TYPE_STRING / DBUS_TYPE_UINT32,
 * value 为指向对应类型变量的指针。
 */
int bctl_prop_set(struct bluectl_ctx *c, const char *path, const char *iface,
		  const char *name, int type, const void *value);

/* Properties.GetAll(path, iface), 返回 a{sv} 应答消息, 失败 NULL */
DBusMessage *bctl_prop_get_all(struct bluectl_ctx *c, const char *path,
			       const char *iface);

/*
 * 在 a{sv} 字典迭代器中查找 name, 命中返回 1 并把 value
 * 定位到变体内容(可直接 get_basic / recurse), 未命中返回 0。
 */
int bctl_dict_lookup(DBusMessageIter *dict, const char *name,
		     DBusMessageIter *value);

/* 迭代器读取(类型不符时保持缺省值) */
int bctl_read_bool(DBusMessageIter *it, int def);
long bctl_read_num(DBusMessageIter *it, long def);
void bctl_read_str(DBusMessageIter *it, char *out, size_t len);

/*
 * 遍历 org.bluez GetManagedObjects() 结果,
 * 对每个 (对象路径, 接口名, 该接口 a{sv} 属性迭代器) 调用 cb。
 * 返回 0 或负数错误码; cb 中迭代器仅在回调期间有效。
 */
typedef void (*bctl_object_cb)(const char *path, const char *iface,
			       DBusMessageIter *props, void *user);
int bctl_foreach_object(struct bluectl_ctx *c, bctl_object_cb cb, void *user);

/*
 * 解析适配器/设备对象路径参数:
 *   adapter: NULL/"" -> 第一个适配器; "hci0" -> /org/bluez/hci0; "/..." 原样
 *   device:  MAC(大小写不敏感) 或 "/..." 原样
 */
int bctl_adapter_path(struct bluectl_ctx *c, const char *adapter,
		      char *out, size_t len);
int bctl_device_path(struct bluectl_ctx *c, const char *device,
		     char *out, size_t len);

/* 从设备对象路径解析 MAC: ".../dev_AA_BB_CC_DD_EE_FF" -> "AA:BB:CC:DD:EE:FF" */
void bctl_mac_from_path(const char *path, char *out, size_t len);

/* 属性填充(供 adapter/device 模块共用) */
void bctl_adapter_fill(DBusMessageIter *props, bluectl_adapter_t *out);
void bctl_device_fill(DBusMessageIter *props, bluectl_device_t *out);

#endif /* _BLUECTL_INTERNAL_H_ */
