/* mini.c - 诊断: 打印 get_adapters/get_devices 实际返回内容(不随组件发布) */
#include <stdio.h>
#include "bluectl.h"

int main(void)
{
	bluectl_adapter_t ads[4];
	bluectl_device_t devs[8];
	int n, i;

	if (bluectl_init() != BLUECTL_OK) {
		printf("init: %s\n", bluectl_last_error());
		return 1;
	}
	n = bluectl_get_adapters(ads, 4);
	printf("get_adapters n=%d\n", n);
	for (i = 0; i < n && i < 4; i++)
		printf("  [%d] path='%s' addr='%s' name='%s' alias='%s' pwr=%d disc=%d discov=%d pair=%d dt=%u pt=%u\n",
		       i, ads[i].path, ads[i].address, ads[i].name, ads[i].alias,
		       ads[i].powered, ads[i].discoverable, ads[i].discovering,
		       ads[i].pairable, ads[i].discoverable_timeout,
		       ads[i].pairable_timeout);

	n = bluectl_get_devices(NULL, devs, 8);
	printf("get_devices n=%d\n", n);
	for (i = 0; i < n && i < 8; i++)
		printf("  [%d] path='%s' addr='%s' name='%s' conn=%d paired=%d rssi=%d uuids=%d\n",
		       i, devs[i].path, devs[i].address, devs[i].name,
		       devs[i].connected, devs[i].paired, devs[i].rssi,
		       devs[i].uuid_count);

	bluectl_deinit();
	return 0;
}
