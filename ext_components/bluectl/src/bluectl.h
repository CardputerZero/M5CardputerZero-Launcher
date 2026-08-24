/*
 * bluectl.h - BlueZ 蓝牙操作库公共 API
 *
 * 通过 system DBus 上的 org.bluez 接口操作蓝牙, 提供
 * bluetoothctl 常用能力的 C API:
 *   - 适配器: 电源/可发现/可配对/别名/扫描
 *   - 设备:   枚举/查询/配对/连接/断开/信任/移除
 *   - 配对代理(agent): PIN/Passkey/确认 回调 (需开 CONFIG_BLUECTL_AGENT_ENABLED)
 *   - 媒体控制(AVRCP MediaPlayer1)      (需开 CONFIG_BLUECTL_MEDIA_ENABLED)
 *
 * 所有接口均为阻塞式 DBus 调用 (bluectl_process() 除外),
 * UI 线程请放到工作线程中调用。
 */
#ifndef _BLUECTL_H_
#define _BLUECTL_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLUECTL_VERSION "0.1.0"

/* 缓冲区尺寸 */
#define BLUECTL_ADDR_LEN   18	/* "XX:XX:XX:XX:XX:XX" + '\0' */
#define BLUECTL_NAME_LEN   64	/* 设备名/别名 */
#define BLUECTL_PATH_LEN   96	/* DBus 对象路径 */
#define BLUECTL_UUID_LEN   37	/* 36 字符 UUID + '\0' */
#define BLUECTL_MAX_UUIDS  12	/* device.uuids[] 固定容量 */

/* 错误码 */
#define BLUECTL_OK              0
#define BLUECTL_ERR             (-1)	/* 一般错误, 详见 bluectl_last_error() */
#define BLUECTL_ERR_NO_CONN     (-2)	/* 未初始化 / DBus 断开 */
#define BLUECTL_ERR_NO_ADAPTER  (-3)	/* 找不到蓝牙适配器 */
#define BLUECTL_ERR_NOT_FOUND   (-4)	/* 对象(设备等)不存在 */
#define BLUECTL_ERR_TIMEOUT     (-5)	/* 调用超时 */
#define BLUECTL_ERR_INVALID_ARG (-6)	/* 参数非法 */
#define BLUECTL_ERR_NO_MEM      (-7)	/* 内存不足 */
#define BLUECTL_ERR_NOT_SUPPORT (-8)	/* 功能未编译(agent/media) */
#define BLUECTL_ERR_NO_BLUEZ   (-9)	/* org.bluez 服务不在线 */

/* ---------------- 生命周期 ---------------- */

/*
 * 连接 system DBus 并注册事件匹配规则, 需先于其它接口调用。
 * 重复调用安全(已连接则直接返回)。
 * 返回 BLUECTL_OK 或负数错误码。
 */
int bluectl_init(void);

/* 释放资源(注销 agent/匹配规则/连接引用), 可重复调用 */
void bluectl_deinit(void);

/* 返回 1 表示 DBus 连接存活 */
int bluectl_is_connected(void);

/* 最近一次错误描述(永不返回 NULL) */
const char *bluectl_last_error(void);

const char *bluectl_version(void);

/*
 * 设置普通方法调用超时(毫秒), pair/connect 仍有独立更长超时。
 * 默认 BLUECTL_DEFAULT_TIMEOUT_MS。
 */
void bluectl_set_timeout(unsigned int timeout_ms);

#define BLUECTL_DEFAULT_TIMEOUT_MS 15000
#define BLUECTL_CONNECT_TIMEOUT_MS 30000
#define BLUECTL_PAIR_TIMEOUT_MS    60000

/*
 * 事件泵: 阻塞等待 DBus I/O 至多 timeout_ms 毫秒, 然后分发
 * 队列中的消息(触发事件回调/agent 回调), 返回分发的消息数。
 * 返回负数表示连接已断开。建议在独立线程或主循环中以小超时轮询。
 */
int bluectl_process(unsigned int timeout_ms);

/* ---------------- 事件通知 ---------------- */

typedef enum {
	BLUECTL_EV_ADAPTER_ADDED = 1,
	BLUECTL_EV_ADAPTER_REMOVED,
	BLUECTL_EV_DEVICE_ADDED,	/* 扫描到新设备或对象出现 */
	BLUECTL_EV_DEVICE_REMOVED,
	BLUECTL_EV_INTERFACES_ADDED,	/* 其他接口对象(如 MediaPlayer1) */
	BLUECTL_EV_INTERFACES_REMOVED,
	BLUECTL_EV_PROPERTY_CHANGED,	/* adapter/device 属性变化 */
	BLUECTL_EV_BLUEZ_UP,		/* org.bluez 服务出现(bluez 启动) */
	BLUECTL_EV_BLUEZ_DOWN,		/* org.bluez 服务退出 */
} bluectl_event_type_t;

/*
 * 事件结构。path/interface/property/value 指向的字符串仅在
 * 回调执行期间有效, 需要保留请自行拷贝。
 */
typedef struct {
	bluectl_event_type_t type;
	const char *path;	/* DBus 对象路径, 可能为 NULL */
	const char *interface;/* 接口名, 如 "org.bluez.Device1" */
	const char *property;	/* PROPERTY_CHANGED: 变化的属性名, 可能为 NULL */
	const char *value;	/* PROPERTY_CHANGED: 属性值字符串形式, 可能为 NULL */
} bluectl_event_t;

typedef void (*bluectl_event_cb_t)(const bluectl_event_t *ev, void *user_data);

/*
 * 注册事件回调。典型用法: 收到 DEVICE_ADDED/PROPERTY_CHANGED 后
 * 重新调用 bluectl_get_devices()/bluectl_get_device() 刷新数据。
 * cb 为 NULL 取消注册。返回 BLUECTL_OK/BLUECTL_ERR_NO_CONN。
 */
int bluectl_set_event_callback(bluectl_event_cb_t cb, void *user_data);

/* ---------------- 适配器(adapter) ---------------- */

typedef struct {
	char path[BLUECTL_PATH_LEN];	/* "/org/bluez/hci0" */
	char address[BLUECTL_ADDR_LEN];
	char name[BLUECTL_NAME_LEN];	/* Name 属性 */
	char alias[BLUECTL_NAME_LEN];	/* Alias 属性(友好名, 可写) */
	int powered;		/* Powered */
	int discoverable;	/* Discoverable */
	int discovering;	/* Discovering: 正在扫描 */
	int pairable;		/* Pairable */
	unsigned int discoverable_timeout;	/* DiscoverableTimeout(秒) */
	unsigned int pairable_timeout;		/* PairableTimeout(秒) */
} bluectl_adapter_t;

/*
 * 适配器参数 adapter 的统一约定(以下各接口同):
 *   NULL/""  - 使用第一个可用适配器
 *   "hci0"   - 自动展开为 "/org/bluez/hci0"
 *   "/org/bluez/hci0" - 按完整对象路径
 */

/* 枚举适配器, 返回写入数量(>=0)或负数错误码; 对应 bluetoothctl `list` */
int bluectl_get_adapters(bluectl_adapter_t *out, int max);

/* 查询单个适配器详情 */
int bluectl_get_adapter(const char *adapter, bluectl_adapter_t *out);

/* 取默认(第一个)适配器对象路径 */
int bluectl_default_adapter(char *path, size_t len);

/* 电源开关, 对应 `power on/off`; BlueZ 无 adapter 时返回 NO_ADAPTER */
int bluectl_set_power(const char *adapter, int on);

int bluectl_set_pairable(const char *adapter, int on);
int bluectl_set_discoverable(const char *adapter, int on);

/* 设置 DiscoverableTimeout(秒), 0 = 永久可发现 */
int bluectl_set_discoverable_timeout(const char *adapter, unsigned int seconds);

int bluectl_set_pairable_timeout(const char *adapter, unsigned int seconds);

/* 修改适配器别名, 对应 bluetoothctl `set-alias`(系统名) */
int bluectl_set_alias(const char *adapter, const char *alias);

/* 开始/停止扫描, 对应 `scan on/off`; 已在扫描时 start 返回 OK */
int bluectl_start_discovery(const char *adapter);
int bluectl_stop_discovery(const char *adapter);

/*
 * 从适配器移除已配对/已知设备(忘掉密钥), 对应 `remove`。
 * device 参数见下一节约定。
 */
int bluectl_remove_device(const char *adapter, const char *device);

/* ---------------- 设备(device) ---------------- */

typedef struct {
	char path[BLUECTL_PATH_LEN];	/* "/org/bluez/hci0/dev_XX_XX_..." */
	char address[BLUECTL_ADDR_LEN];
	char name[BLUECTL_NAME_LEN];	/* Alias 优先, 否则 Name */
	char icon[32];		/* Icon, 如 "phone" */
	int connected;		/* Connected */
	int paired;		/* Paired */
	int trusted;		/* Trusted */
	int blocked;		/* Blocked */
	int rssi;		/* RSSI(dBm), 未知为 0 */
	unsigned int dev_class;	/* Class */
	char uuids[BLUECTL_MAX_UUIDS][BLUECTL_UUID_LEN];	/* 服务 UUID */
	int uuid_count;		/* UUID 总数(可能大于 uuids[] 容量, 超出截断) */
} bluectl_device_t;

/*
 * 设备参数 device 的统一约定(以下各接口同):
 *   "AA:BB:CC:DD:EE:FF" - 按 MAC 地址解析(大小写不敏感)
 *   "/org/bluez/hci0/dev_AA_BB_..." - 按完整对象路径
 */

/*
 * 枚举已知设备(含扫描结果), 对应 `devices`。
 * adapter 为 NULL 时返回所有适配器下设备。
 * 仅返回已解析出 MAC 的 Device1 对象, 按路径排序。
 */
int bluectl_get_devices(const char *adapter, bluectl_device_t *out, int max);

/* 查询单个设备详情, 对应 `info` */
int bluectl_get_device(const char *device, bluectl_device_t *out);

/*
 * 配对, 对应 `pair`。阻塞至配对完成(最长 BLUECTL_PAIR_TIMEOUT_MS),
 * 期间 agent 回调可能在 bluectl_process() 中触发, 请保证事件泵在跑。
 */
int bluectl_pair(const char *device);

/* 连接/断开, 对应 `connect`/`disconnect` */
int bluectl_connect(const char *device);
int bluectl_disconnect(const char *device);

/* 信任/屏蔽, 对应 `trust`/`untrust`/`block`/`unblock` */
int bluectl_set_trusted(const char *device, int on);
int bluectl_set_blocked(const char *device, int on);

/* 修改设备别名, 对应 bluetoothctl `set-alias` 设备形式 */
int bluectl_set_device_alias(const char *device, const char *alias);

/* ---------------- 配对代理 agent ---------------- */

#ifdef CONFIG_BLUECTL_AGENT_ENABLED

#define BLUECTL_AGENT_PATH "/org/bluectl/agent"

/* agent capability 取值(见 BlueZ 文档) */
#define BLUECTL_CAP_DISPLAY_ONLY      "DisplayOnly"
#define BLUECTL_CAP_DISPLAY_YES_NO    "DisplayYesNo"
#define BLUECTL_CAP_KEYBOARD_ONLY     "KeyboardOnly"
#define BLUECTL_CAP_NO_INPUT_NO_OUTPUT "NoInputNoOutput"
#define BLUECTL_CAP_KEYBOARD_DISPLAY  "KeyboardDisplay"

/*
 * agent 请求(BlueZ 调用我们的 org.bluez.Agent1 方法)。
 * needs_reply 为 1 时应用稍后必须调用 bluectl_agent_reply(),
 * 否则配对流程会一直挂起直到 BlueZ 超时。
 */
typedef struct {
	unsigned long id;	/* 请求 id, 用于 bluectl_agent_reply() */
	const char *method;	/* "RequestPinCode" 等 */
	char device[BLUECTL_ADDR_LEN];	/* 从对象路径解析的设备 MAC */
	char path[BLUECTL_PATH_LEN];	/* 设备对象路径 */
	char passkey[8];	/* RequestConfirmation/DisplayPasskey: 数字 */
	char uuid[BLUECTL_UUID_LEN];	/* AuthorizeService: 服务 UUID */
	int needs_reply;	/* 是否等待 bluectl_agent_reply() */
} bluectl_agent_request_t;

typedef void (*bluectl_agent_cb_t)(const bluectl_agent_request_t *req, void *user_data);

/*
 * 注册 agent 请求回调(未设置时配对请求会被自动拒绝)。
 * 通常在 bluectl_agent_register() 之前调用。cb 为 NULL 取消。
 */
int bluectl_agent_set_callback(bluectl_agent_cb_t cb, void *user_data);

/*
 * 注册配对代理, 对应 `agent on`。
 * capability 为 NULL 时使用 BLUECTL_CAP_KEYBOARD_DISPLAY。
 * 依赖 bluectl_process() 驱动请求分发。
 */
int bluectl_agent_register(const char *capability);

/* 设为系统默认 agent, 对应 `default-agent` */
int bluectl_agent_request_default(void);

/* 注销 agent, 对应 `agent off`; 未注册时安全调用 */
int bluectl_agent_unregister(void);

/*
 * 应答 agent 请求。
 * accept=1: RequestPinCode 传 PIN 字符串, RequestPasskey 传数字字符串,
 *           其余(确认/授权) code 忽略。
 * accept=0: 向 BlueZ 返回 Rejected。
 */
int bluectl_agent_reply(unsigned long id, int accept, const char *code);

/* 取消所有挂起的 agent 请求(向 BlueZ 返回 Canceled), 如 UI 退出时 */
int bluectl_agent_cancel_pending(void);

#endif /* CONFIG_BLUECTL_AGENT_ENABLED */

/* ---------------- 媒体控制 AVRCP ---------------- */

#ifdef CONFIG_BLUECTL_MEDIA_ENABLED

typedef struct {
	char path[BLUECTL_PATH_LEN];	/* player 对象路径 */
	char device[BLUECTL_ADDR_LEN];	/* 所属设备 MAC */
	char status[16];	/* playing/paused/stopped/... */
	char title[BLUECTL_NAME_LEN];
	char artist[BLUECTL_NAME_LEN];
	char album[BLUECTL_NAME_LEN];
	char genre[32];
	unsigned int duration;	/* 当前曲目时长 ms */
	unsigned int position;	/* 播放进度 ms(查询时快照) */
} bluectl_media_player_t;

/*
 * 查询设备(或 player 完整路径)的媒体播放器状态。
 * device 参数同设备约定; 返回第一个找到的 player。
 */
int bluectl_media_player_get(const char *device, bluectl_media_player_t *out);

/*
 * 媒体控制命令, cmd 为以下之一:
 *   "play" "pause" "stop" "next" "previous" "fastforward" "rewind"
 */
int bluectl_media_cmd(const char *device, const char *cmd);

#endif /* CONFIG_BLUECTL_MEDIA_ENABLED */

#ifdef __cplusplus
}
#endif

#endif /* _BLUECTL_H_ */
