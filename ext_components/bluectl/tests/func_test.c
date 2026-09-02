/*
 * func_test.c - bluectl 功能测试: 连接 mock org.bluez 服务,
 * 覆盖 adapter/device/agent/media/事件 全链路
 */
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bluectl.h"

static int g_fail;
static int g_events;
static int g_agent_reqs;
static volatile int g_run = 1;

#define CHECK(desc, cond) do { \
	printf("%-52s %s\n", desc, (cond) ? "PASS" : "FAIL"); \
	if (!(cond)) g_fail++; \
} while (0)

static void on_event(const bluectl_event_t *ev, void *user)
{
	(void)user;
	g_events++;
	if (ev->type == BLUECTL_EV_PROPERTY_CHANGED)
		printf("  [ev] prop %-20s = %s\n", ev->property ? ev->property : "-",
		       ev->value ? ev->value : "-");
	else
		printf("  [ev] type=%d path=%s iface=%s\n", ev->type,
		       ev->path ? ev->path : "-",
		       ev->interface ? ev->interface : "-");
}

static void on_agent(const bluectl_agent_request_t *req, void *user)
{
	(void)user;
	g_agent_reqs++;
	printf("  [agent] %s device=%s passkey=%s needs_reply=%d\n",
	       req->method, req->device, req->passkey, req->needs_reply);
	if (req->needs_reply) {
		int rv = bluectl_agent_reply(req->id, 1, "1234");

		printf("  [agent] reply rv=%d err='%s'\n", rv,
		       bluectl_last_error());
	}
}

static void *pump_thread(void *arg)
{
	(void)arg;
	while (g_run)
		bluectl_process(50);
	return NULL;
}

int main(void)
{
	bluectl_adapter_t ad;
	bluectl_adapter_t ads[4];
	bluectl_device_t devs[8];
	bluectl_device_t dev;
	pthread_t pump;
	int rv, n;

	setvbuf(stdout, NULL, _IONBF, 0);

	rv = bluectl_init();
	CHECK("init", rv == BLUECTL_OK);

	rv = bluectl_set_event_callback(on_event, NULL);
	CHECK("set_event_callback", rv == BLUECTL_OK);

	/* ---- 适配器 ---- */
	n = bluectl_get_adapters(ads, 4);
	CHECK("get_adapters returns 1", n == 1);
	CHECK("adapter address", n >= 1 && !strcmp(ads[0].address, "00:11:22:33:44:55"));
	CHECK("adapter path", n >= 1 && !strcmp(ads[0].path, "/org/bluez/hci0"));
	CHECK("adapter powered=0", n >= 1 && ads[0].powered == 0);
	CHECK("adapter discoverable_timeout=180", n >= 1 && ads[0].discoverable_timeout == 180);

	rv = bluectl_get_adapter(NULL, &ad);
	CHECK("get_adapter(default)", rv == BLUECTL_OK && !strcmp(ad.alias, "test-host"));

	rv = bluectl_set_power(NULL, 1);
	CHECK("set_power on", rv == BLUECTL_OK);
	rv = bluectl_get_adapter(NULL, &ad);
	CHECK("power state persisted", ad.powered == 1);

	rv = bluectl_set_discoverable("hci0", 1);
	CHECK("set_discoverable(hci0)", rv == BLUECTL_OK);
	rv = bluectl_set_alias(NULL, "new-name");
	CHECK("set_alias", rv == BLUECTL_OK);
	rv = bluectl_get_adapter(NULL, &ad);
	CHECK("alias persisted", !strcmp(ad.alias, "new-name"));

	rv = bluectl_set_discoverable_timeout(NULL, 60);
	CHECK("set_discoverable_timeout", rv == BLUECTL_OK);
	rv = bluectl_set_pairable_timeout(NULL, 30);
	CHECK("set_pairable_timeout", rv == BLUECTL_OK);

	rv = bluectl_start_discovery(NULL);
	CHECK("start_discovery", rv == BLUECTL_OK);
	rv = bluectl_get_adapter(NULL, &ad);
	CHECK("discovering=1", ad.discovering == 1);
	rv = bluectl_start_discovery(NULL);
	CHECK("start_discovery idempotent-ish", rv == BLUECTL_OK);

	/* ---- 设备 ---- */
	n = bluectl_get_devices(NULL, devs, 8);
	CHECK("get_devices returns 2", n == 2);
	CHECK("devices sorted by path", n >= 2 &&
	      strcmp(devs[0].path, devs[1].path) < 0);
	CHECK("dev2 mac", n >= 1 && !strcmp(devs[0].address, "11:22:33:44:55:66"));
	CHECK("dev2 connected", n >= 1 && devs[0].connected == 1);
	CHECK("dev2 paired", n >= 1 && devs[0].paired == 1);
	CHECK("dev1 mac", n >= 2 && !strcmp(devs[1].address, "AA:BB:CC:DD:EE:FF"));
	CHECK("dev1 name", n >= 2 && !strcmp(devs[1].name, "Test Phone"));
	CHECK("dev1 rssi", n >= 2 && devs[1].rssi == -52);
	CHECK("dev1 uuid_count", n >= 2 && devs[1].uuid_count == 1);

	rv = bluectl_get_device("aa:bb:cc:dd:ee:ff", &dev);
	CHECK("get_device by lower-case MAC", rv == BLUECTL_OK);
	CHECK("get_device path", rv == BLUECTL_OK && !strcmp(dev.path, "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF"));
	CHECK("get_device icon", rv == BLUECTL_OK && !strcmp(dev.icon, "phone"));
	CHECK("get_device dev_class", rv == BLUECTL_OK && dev.dev_class == 0x260304);

	rv = bluectl_get_device("/org/bluez/hci0/dev_11_22_33_44_55_66", &dev);
	CHECK("get_device by path", rv == BLUECTL_OK && !strcmp(dev.name, "Speaker"));

	rv = bluectl_get_device("99:99:99:99:99:99", &dev);
	CHECK("get_device unknown MAC -> NOT_FOUND", rv == BLUECTL_ERR_NOT_FOUND);

	rv = bluectl_set_trusted("AA:BB:CC:DD:EE:FF", 1);
	CHECK("set_trusted", rv == BLUECTL_OK);
	rv = bluectl_set_device_alias("AA:BB:CC:DD:EE:FF", "Renamed");
	CHECK("set_device_alias", rv == BLUECTL_OK);
	rv = bluectl_set_blocked("AA:BB:CC:DD:EE:FF", 0);
	CHECK("set_blocked off", rv == BLUECTL_OK);

	rv = bluectl_connect("AA:BB:CC:DD:EE:FF");
	CHECK("connect", rv == BLUECTL_OK);
	rv = bluectl_disconnect("AA:BB:CC:DD:EE:FF");
	CHECK("disconnect", rv == BLUECTL_OK);

	/* ---- agent + 配对 ---- */
	rv = bluectl_agent_set_callback(on_agent, NULL);
	CHECK("agent_set_callback", rv == BLUECTL_OK);
	rv = bluectl_agent_register(NULL);
	CHECK("agent_register", rv == BLUECTL_OK);
	rv = bluectl_agent_request_default();
	CHECK("agent_request_default", rv == BLUECTL_OK);

	pthread_create(&pump, NULL, pump_thread, NULL);
	usleep(200000);	/* 让事件泵启动, 等 mock 的 InterfacesAdded(300ms 后再等一轮) */

	rv = bluectl_pair("AA:BB:CC:DD:EE:FF");
	if (rv != BLUECTL_OK)
		printf("  pair err: %s\n", bluectl_last_error());
	CHECK("pair with agent confirmation", rv == BLUECTL_OK);
	usleep(200000);	/* 等配对后的 Paired 属性变化事件 */
	CHECK("agent RequestConfirmation seen", g_agent_reqs >= 1);

	rv = bluectl_get_device("AA:BB:CC:DD:EE:FF", &dev);
	CHECK("paired persisted", rv == BLUECTL_OK && dev.paired == 1);

	/* ---- 媒体 ---- */
#ifdef CONFIG_BLUECTL_MEDIA_ENABLED
	{
		bluectl_media_player_t mp;

		rv = bluectl_media_player_get("11:22:33:44:55:66", &mp);
		CHECK("media_player_get by MAC", rv == BLUECTL_OK);
		CHECK("media status", rv == BLUECTL_OK && !strcmp(mp.status, "playing"));
		CHECK("media title", rv == BLUECTL_OK && !strcmp(mp.title, "Test Song"));
		CHECK("media duration", rv == BLUECTL_OK && mp.duration == 210000);
		CHECK("media position", rv == BLUECTL_OK && mp.position == 42000);

		rv = bluectl_media_cmd("11:22:33:44:55:66", "pause");
		CHECK("media_cmd pause", rv == BLUECTL_OK);
		rv = bluectl_media_cmd("11:22:33:44:55:66", "bogus");
		CHECK("media_cmd bogus -> INVALID_ARG", rv == BLUECTL_ERR_INVALID_ARG);
		rv = bluectl_media_player_get("AA:BB:CC:DD:EE:FF", &mp);
		CHECK("media_player_get no player -> NOT_FOUND", rv == BLUECTL_ERR_NOT_FOUND);
	}
#endif

	/* ---- 移除/停止 ---- */
	rv = bluectl_remove_device(NULL, "AA:BB:CC:DD:EE:FF");
	CHECK("remove_device", rv == BLUECTL_OK);
	rv = bluectl_stop_discovery(NULL);
	CHECK("stop_discovery", rv == BLUECTL_OK);
	rv = bluectl_get_adapter(NULL, &ad);
	CHECK("discovering=0", ad.discovering == 0);
	rv = bluectl_stop_discovery(NULL);
	CHECK("stop_discovery when stopped -> OK", rv == BLUECTL_OK);

	usleep(300000);	/* 收尾事件 */
	g_run = 0;
	pthread_join(pump, NULL);
	CHECK("events received", g_events >= 4);

	rv = bluectl_agent_unregister();
	CHECK("agent_unregister", rv == BLUECTL_OK);

	bluectl_deinit();
	printf("\n%s (%d failures, %d events, %d agent requests)\n",
	       g_fail ? "RESULT: FAIL" : "RESULT: ALL PASS", g_fail, g_events,
	       g_agent_reqs);
	return g_fail ? 1 : 0;
}
