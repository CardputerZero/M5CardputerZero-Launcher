# T04：Bluetooth

**固定 worker 目录**：`/home/nihao/w2T/github/launcher/projects/ui_test4`

## 目标

将新 Bluetooth 页面完全对接真实 backend contract，覆盖电源、Discoverable、别名、已连接设备、扫描、配对、连接、断开和删除。

## 负责文件

- `settings_bluetooth_page.hpp`
- `settings_bluetooth_connected_devices_page.hpp`
- `settings_bluetooth_scan_page.hpp`
- 新增 Bluetooth adapter、decoder 和测试。

## API

- `BtStatus`
- `BtPower 0|1`
- `BtDiscoverable 0|1`
- `BtAlias text`，长度小于 64 字节且不得含控制字符。
- `BtScan max`
- `BtList max`
- `BtConnectedList max`
- `BtPair address`
- `BtConnect address`
- `BtDisconnect address`
- `BtRemove address`
- 或完整使用 `BtSessionInit`、`BtStatusGet`、`BtScanOn/Off` 等 session API。

## 工作内容

1. 核对现有页面命令名、参数和返回字段。
2. 严格解析制表符状态和设备记录。
3. 电源关闭时阻止 Discoverable、Connected、Scan。
4. session 初始化/销毁、扫描启停必须成对。
5. 保存别名后刷新树节点文本。

## 异步要求

- 复用当前页面已有 dispatch 队列方向，但补齐 generation、pending 和失败回滚。
- backend callback 不得直接操作 LVGL。
- 析构顺序必须是移除键盘事件、删除 timer、阻止入队、停止扫描、join 任务、删除 LVGL 对象。

## 验收

- 电源开关成功/失败都能正确回滚。
- Discoverable 在 Bluetooth 关闭时显示提示而不是发送错误请求。
- 已连接设备和扫描设备列表独立工作。
- 配对、连接、断开、删除有成功和失败反馈。
- UTF‑8 别名输入、删除、保存、取消和失败回滚可用。
