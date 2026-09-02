/*
 * bluectl_demo.c - bluectl 交互式命令行演示程序
 *
 * 演示 bluectl 库的主要能力: 适配器枚举/电源/扫描、设备枚举/查询/配对/
 * 连接/信任/移除、配对代理 agent(自动应答)、媒体控制(AVRCP)、事件回调。
 *
 * 编译(本机, 开启全部功能, pkg-config dbus-1 方式):
 *   gcc -std=gnu99 -Wall -Wextra -Wno-unused-parameter \
 *       -DCONFIG_BLUECTL_AGENT_ENABLED=1 -DCONFIG_BLUECTL_MEDIA_ENABLED=1 \
 *       -I../src $(pkg-config --cflags dbus-1) \
 *       bluectl_demo.c \
 *       ../src/bluectl_core.c ../src/bluectl_adapter.c \
 *       ../src/bluectl_device.c ../src/bluectl_agent.c ../src/bluectl_media.c \
 *       -o bluectl_demo $(pkg-config --libs dbus-1) -lpthread
 *
 * 运行(需 org.bluez 在线, 可用 tests/mock_bluez.py 模拟):
 *   ./bluectl_demo            # 交互式, 输入 help 查看子命令
 *   echo "devices" | ./bluectl_demo   # 单命令脚本式
 *
 * 说明:
 *   - 操作接口(pair/connect 等)为阻塞式 DBus 调用, 见 bluectl.h;
 *   - 事件与 agent 回调由 bluectl_process() 驱动, 本 demo 用一个
 *     独立线程跑事件泵, 这样 pair/connect 阻塞期间 agent 仍能应答;
 *   - agent 请求自动打印并接受(演示用途, 生产代码应弹窗征求用户意见)。
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bluectl.h"

#define MAX_ADAPTERS 8
#define MAX_DEVICES  32

static volatile int g_run = 1;

/* ---------------- 事件回调 ---------------- */

static const char *event_name(int type)
{
	switch (type) {
	case BLUECTL_EV_ADAPTER_ADDED:      return "ADAPTER_ADDED";
	case BLUECTL_EV_ADAPTER_REMOVED:    return "ADAPTER_REMOVED";
	case BLUECTL_EV_DEVICE_ADDED:       return "DEVICE_ADDED";
	case BLUECTL_EV_DEVICE_REMOVED:     return "DEVICE_REMOVED";
	case BLUECTL_EV_INTERFACES_ADDED:   return "INTERFACES_ADDED";
	case BLUECTL_EV_INTERFACES_REMOVED: return "INTERFACES_REMOVED";
	case BLUECTL_EV_PROPERTY_CHANGED:   return "PROPERTY_CHANGED";
	case BLUECTL_EV_BLUEZ_UP:           return "BLUEZ_UP";
	case BLUECTL_EV_BLUEZ_DOWN:         return "BLUEZ_DOWN";
	default:                            return "UNKNOWN";
	}
}

static void on_event(const bluectl_event_t *ev, void *user)
{
	(void)user;
	printf("[ev] %-18s path=%-36s iface=%-24s",
		   event_name(ev->type),
		   ev->path ? ev->path : "-",
		   ev->interface ? ev->interface : "-");
	if (ev->property)
		printf(" prop=%s", ev->property);
	if (ev->value)
		printf(" val=%s", ev->value);
	printf("\n");
}

/* ---------------- agent 回调 ---------------- */

#ifdef CONFIG_BLUECTL_AGENT_ENABLED
static void on_agent(const bluectl_agent_request_t *req, void *user)
{
	(void)user;
	printf("[agent] %-24s device=%-18s passkey=%-8s needs_reply=%d\n",
		   req->method, req->device, req->passkey, req->needs_reply);
	if (!req->needs_reply)
		return;	/* 展示型请求(DisplayPasskey 等), 无需应答 */
	/*
	 * 自动接受: PIN/Passkey 给 "1234", 其余(确认/授权)直接同意。
	 * 演示用; 生产代码应在此征求用户意见后再调用 bluectl_agent_reply()。
	 */
	if (!strcmp(req->method, "RequestPinCode") ||
		!strcmp(req->method, "RequestPasskey"))
		bluectl_agent_reply(req->id, 1, "1234");
	else
		bluectl_agent_reply(req->id, 1, NULL);
}
#endif /* CONFIG_BLUECTL_AGENT_ENABLED */

/* ---------------- 事件泵线程 ---------------- */

static void *pump_thread(void *arg)
{
	(void)arg;
	while (g_run) {
		int n = bluectl_process(100);

		if (n < 0) {	/* 连接断开 */
			printf("[pump] DBus 连接断开: %s\n",
				   bluectl_last_error());
			break;
		}
	}
	return NULL;
}

/* ---------------- 命令实现 ---------------- */

static void usage(void)
{
	printf("子命令:\n");
	printf("  list                       枚举适配器\n");
	printf("  devices                    枚举设备\n");
	printf("  info <mac>                 查询设备详情\n");
	printf("  power on|off               适配器电源\n");
	printf("  scan on|off                开始/停止扫描\n");
	printf("  pair <mac>                 配对(阻塞至完成, 最长 60s)\n");
	printf("  connect <mac>              连接设备\n");
	printf("  disconnect <mac>           断开连接\n");
	printf("  trust <mac>                信任设备\n");
	printf("  remove <mac>               移除设备\n");
#ifdef CONFIG_BLUECTL_AGENT_ENABLED
	printf("  agent on|off               注册/注销配对代理(自动接受请求)\n");
#else
	printf("  agent on|off               (未编译 CONFIG_BLUECTL_AGENT_ENABLED)\n");
#endif
#ifdef CONFIG_BLUECTL_MEDIA_ENABLED
	printf("  media <mac> status                    查询媒体播放器(AVRCP)\n");
	printf("  media <mac> play|pause|stop|next|previous\n");
	printf("  media <mac> fastforward|rewind         AVRCP 媒体控制\n");
#else
	printf("  media ...                  (未编译 CONFIG_BLUECTL_MEDIA_ENABLED)\n");
#endif
	printf("  help                       显示本帮助\n");
	printf("  quit                       退出\n");
}

static void print_adapter(const bluectl_adapter_t *a)
{
	printf("  %s\n", a->path);
	printf("    address: %s  name: %s  alias: %s\n",
		   a->address, a->name, a->alias);
	printf("    powered=%d discoverable=%d discovering=%d pairable=%d\n",
		   a->powered, a->discoverable, a->discovering, a->pairable);
	printf("    discoverable_timeout=%us pairable_timeout=%us\n",
		   a->discoverable_timeout, a->pairable_timeout);
}

static void print_device(const bluectl_device_t *d)
{
	printf("  %s  %s\n", d->address, d->name[0] ? d->name : "(无名称)");
	printf("    path: %s\n", d->path);
	printf("    icon: %s  class: 0x%06x  rssi: %d dBm\n",
		   d->icon[0] ? d->icon : "-", d->dev_class, d->rssi);
	printf("    connected=%d paired=%d trusted=%d blocked=%d\n",
		   d->connected, d->paired, d->trusted, d->blocked);
	printf("    uuids(%d):", d->uuid_count);
	if (!d->uuid_count)
		printf(" -\n");
	else {
		int i;

		for (i = 0; i < d->uuid_count && i < BLUECTL_MAX_UUIDS; i++)
			printf(" %s", d->uuids[i]);
		printf("\n");
	}
}

static int cmd_list(void)
{
	bluectl_adapter_t ads[MAX_ADAPTERS];
	int n, i;

	n = bluectl_get_adapters(ads, MAX_ADAPTERS);
	if (n < 0) {
		printf("list 失败: %s\n", bluectl_last_error());
		return 0;
	}
	printf("适配器 %d 个:\n", n);
	for (i = 0; i < n; i++)
		print_adapter(&ads[i]);
	return 0;
}

static int cmd_devices(void)
{
	bluectl_device_t devs[MAX_DEVICES];
	int n, i;

	n = bluectl_get_devices(NULL, devs, MAX_DEVICES);
	if (n < 0) {
		printf("devices 失败: %s\n", bluectl_last_error());
		return 0;
	}
	printf("设备 %d 个:\n", n);
	for (i = 0; i < n; i++)
		print_device(&devs[i]);
	return 0;
}

static int cmd_info(const char *mac)
{
	bluectl_device_t dev;
	int rc;

	rc = bluectl_get_device(mac, &dev);
	if (rc < 0) {
		printf("info %s 失败: %s\n", mac, bluectl_last_error());
		return 0;
	}
	print_device(&dev);
	return 0;
}

static int cmd_power(const char *onoff)
{
	int on;

	if (!strcmp(onoff, "on"))
		on = 1;
	else if (!strcmp(onoff, "off"))
		on = 0;
	else {
		printf("power 参数须为 on|off\n");
		return 0;
	}
	if (bluectl_set_power(NULL, on) < 0)
		printf("power %s 失败: %s\n", onoff, bluectl_last_error());
	else
		printf("power %s OK\n", onoff);
	return 0;
}

static int cmd_scan(const char *onoff)
{
	if (!strcmp(onoff, "on")) {
		if (bluectl_start_discovery(NULL) < 0)
			printf("scan on 失败: %s\n", bluectl_last_error());
		else
			printf("scan on OK\n");
	} else if (!strcmp(onoff, "off")) {
		if (bluectl_stop_discovery(NULL) < 0)
			printf("scan off 失败: %s\n", bluectl_last_error());
		else
			printf("scan off OK\n");
	} else {
		printf("scan 参数须为 on|off\n");
	}
	return 0;
}

static int cmd_pair(const char *mac)
{
	int rc;

	printf("pair %s ... (阻塞至完成, 最长 60s, 期间事件泵驱动 agent)\n",
		   mac);
	rc = bluectl_pair(mac);
	if (rc < 0)
		printf("pair %s 失败: %s\n", mac, bluectl_last_error());
	else
		printf("pair %s OK\n", mac);
	return 0;
}

static int cmd_connect(const char *mac)
{
	if (bluectl_connect(mac) < 0)
		printf("connect %s 失败: %s\n", mac, bluectl_last_error());
	else
		printf("connect %s OK\n", mac);
	return 0;
}

static int cmd_disconnect(const char *mac)
{
	if (bluectl_disconnect(mac) < 0)
		printf("disconnect %s 失败: %s\n", mac, bluectl_last_error());
	else
		printf("disconnect %s OK\n", mac);
	return 0;
}

static int cmd_trust(const char *mac)
{
	if (bluectl_set_trusted(mac, 1) < 0)
		printf("trust %s 失败: %s\n", mac, bluectl_last_error());
	else
		printf("trust %s OK\n", mac);
	return 0;
}

static int cmd_remove(const char *mac)
{
	if (bluectl_remove_device(NULL, mac) < 0)
		printf("remove %s 失败: %s\n", mac, bluectl_last_error());
	else
		printf("remove %s OK\n", mac);
	return 0;
}

#ifdef CONFIG_BLUECTL_AGENT_ENABLED
static int cmd_agent(const char *onoff)
{
	if (!strcmp(onoff, "on")) {
		int rc = bluectl_agent_register(NULL);

		if (rc < 0) {
			printf("agent on 失败: %s\n", bluectl_last_error());
			return 0;
		}
		rc = bluectl_agent_request_default();
		if (rc < 0)
			printf("default-agent 失败: %s\n", bluectl_last_error());
		else
			printf("agent on OK (已设为默认)\n");
	} else if (!strcmp(onoff, "off")) {
		bluectl_agent_unregister();
		printf("agent off OK\n");
	} else {
		printf("agent 参数须为 on|off\n");
	}
	return 0;
}
#else
static int cmd_agent(const char *onoff)
{
	(void)onoff;
	printf("agent 未编译: 需 CONFIG_BLUECTL_AGENT_ENABLED\n");
	return 0;
}
#endif /* CONFIG_BLUECTL_AGENT_ENABLED */

#ifdef CONFIG_BLUECTL_MEDIA_ENABLED
static int cmd_media(const char *mac, const char *cmd)
{
	if (!strcmp(cmd, "play") || !strcmp(cmd, "pause") ||
		!strcmp(cmd, "stop") || !strcmp(cmd, "next") ||
		!strcmp(cmd, "previous") || !strcmp(cmd, "fastforward") ||
		!strcmp(cmd, "rewind")) {
		if (bluectl_media_cmd(mac, cmd) < 0)
			printf("media %s %s 失败: %s\n", mac, cmd,
				   bluectl_last_error());
		else
			printf("media %s %s OK\n", mac, cmd);
		return 0;
	}
	if (strcmp(cmd, "status") && strcmp(cmd, "info")) {
		printf("media 参数须为 status|play|pause|stop|next|previous|fastforward|rewind\n");
		return 0;
	}
	{
		bluectl_media_player_t mp;
		int rc = bluectl_media_player_get(mac, &mp);

		if (rc < 0) {
			printf("media %s 查询失败: %s\n", mac,
				   bluectl_last_error());
			return 0;
		}
		printf("player %s (device %s):\n", mp.path, mp.device);
		printf("  status=%s title=%s artist=%s album=%s\n",
			   mp.status, mp.title, mp.artist, mp.album);
		printf("  duration=%ums position=%ums\n",
			   mp.duration, mp.position);
	}
	return 0;
}
#else
static int cmd_media(const char *mac, const char *cmd)
{
	(void)mac;
	(void)cmd;
	printf("media 未编译: 需 CONFIG_BLUECTL_MEDIA_ENABLED\n");
	return 0;
}
#endif /* CONFIG_BLUECTL_MEDIA_ENABLED */

/* ---------------- 主循环 ---------------- */

static int run_command(char *line)
{
	char *argv[4];
	int argc = 0;
	char *p = line;

	/* 按空白拆分命令行(最多 4 个参数) */
	while (*p && argc < 4) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (!*p)
			break;
		argv[argc++] = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\n')
			p++;
		if (*p)
			*p++ = '\0';
	}
	if (!argc)
		return 0;	/* 空行 */

	if (!strcmp(argv[0], "help") || !strcmp(argv[0], "?")) {
		usage();
		return 0;
	}
	if (!strcmp(argv[0], "quit") || !strcmp(argv[0], "exit"))
		return 1;
	if (!strcmp(argv[0], "list"))
		return cmd_list();
	if (!strcmp(argv[0], "devices"))
		return cmd_devices();
	if (!strcmp(argv[0], "info") && argc >= 2)
		return cmd_info(argv[1]);
	if (!strcmp(argv[0], "power") && argc >= 2)
		return cmd_power(argv[1]);
	if (!strcmp(argv[0], "scan") && argc >= 2)
		return cmd_scan(argv[1]);
	if (!strcmp(argv[0], "pair") && argc >= 2)
		return cmd_pair(argv[1]);
	if (!strcmp(argv[0], "connect") && argc >= 2)
		return cmd_connect(argv[1]);
	if (!strcmp(argv[0], "disconnect") && argc >= 2)
		return cmd_disconnect(argv[1]);
	if (!strcmp(argv[0], "trust") && argc >= 2)
		return cmd_trust(argv[1]);
	if (!strcmp(argv[0], "remove") && argc >= 2)
		return cmd_remove(argv[1]);
	if (!strcmp(argv[0], "agent") && argc >= 2)
		return cmd_agent(argv[1]);
	if (!strcmp(argv[0], "media") && argc >= 3)
		return cmd_media(argv[1], argv[2]);
	printf("未知命令: %s (输入 help 查看帮助)\n", argv[0]);
	return 0;
}

int main(void)
{
	pthread_t pump;
	char line[256];

	setvbuf(stdout, NULL, _IONBF, 0);

	if (bluectl_init() != BLUECTL_OK) {
		printf("bluectl_init 失败: %s\n", bluectl_last_error());
		return 1;
	}
	bluectl_set_event_callback(on_event, NULL);
#ifdef CONFIG_BLUECTL_AGENT_ENABLED
	bluectl_agent_set_callback(on_agent, NULL);
#endif

	/* 事件泵线程: 驱动事件与 agent 回调 */
	if (pthread_create(&pump, NULL, pump_thread, NULL) != 0) {
		printf("创建事件泵线程失败\n");
		bluectl_deinit();
		return 1;
	}

	printf("bluectl demo %s - 输入 help 查看帮助, quit 退出\n",
		   bluectl_version());

	for (;;) {
		int quit;

		printf("bluectl> ");
		fflush(stdout);
		if (!fgets(line, sizeof(line), stdin))
			break;	/* EOF */
		quit = run_command(line);
		if (quit)
			break;
	}

	g_run = 0;
	pthread_join(pump, NULL);
	bluectl_deinit();
	printf("bye\n");
	return 0;
}
