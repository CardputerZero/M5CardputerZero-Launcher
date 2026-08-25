# T03：Wi‑Fi

**固定 worker 目录**：`/home/nihao/w2T/github/launcher/projects/ui_test3`

## 目标

将新 `settings_wifi_page.hpp` 从 Mock Wi‑Fi 改为真实 `cp0_lvgl` API，同时保留旧 UI 的扫描、连接、隐藏网络、密码和忘记网络行为。

## 负责文件

- `settings_wifi_page.hpp`
- 新增 `settings_wifi_api.hpp/.cpp`、`settings_wifi_model.hpp/.cpp` 或 Wi‑Fi 测试。

不得修改 `settings_page.cpp`；中央树绑定由 T00 完成。

## API

- `RadioEnabled`
- `RadioSetEnabled on|off`
- `Status`
- `Scan [max]`
- `Connect ssid [password]`
- `ConnectHidden ssid [password]`
- `ProfileForget ssid`
- 必要时使用 `ProfileDisconnectActive`。

扫描 payload 格式为：`ssid:signal:security:in_use:saved`；状态 payload 格式为：`connected:ssid:ip:signal:ethernet`。

## 必须替换

- `MockAccessPoint`
- `MockError`
- `initialize_mock_data()`
- `mock_networks_`
- `mock_connected_ssid_`
- Mock 扫描、连接、忘记网络逻辑。

## 必须保留

- Wi‑Fi 关闭时的 power warning。
- 开放网络、已保存网络、密码网络、隐藏网络四种连接路径。
- 认证失败、网络不存在、IP 配置失败、超时、服务不可用提示。
- 扫描/连接/退出时停止任务。

## 异步要求

- callback 只入队，不访问 LVGL。
- 页面使用 lifetime token、generation 和 pending 状态。
- 退出页面时停止扫描/连接并等待任务结束。
- 旧扫描结果不能覆盖新扫描结果。

## 验收

- 扫描显示真实 SSID、信号、加密类型、当前连接和 saved 状态。
- 连接成功后显示真实 SSID/IP。
- 密码错误不误报成功。
- 连接中按 ESC、页面反复进入退出均安全。
- 忘记当前网络后状态正确刷新。
