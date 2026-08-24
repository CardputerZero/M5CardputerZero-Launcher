# Settings UI 接口迁移任务分配书

> 本总方案已经按人员拆分为独立任务书，分发入口见：
> `/home/nihao/work/launcher/docs/settings_ui_tasks/README.md`。
> 每名程序员应阅读公共规范后，只领取对应的 `Txx-*.md` 文件。

## 1. 目标与范围

本任务的目标是：保留新 Settings UI 的页面结构、视觉效果、键盘交互和异步页面能力，将它接入旧 Settings UI 已验证过的业务接口及 `cp0_lvgl` 后端 API，最终替换 APPLaunch 当前的旧 Settings 页面。

本任务不是把旧 UI 的绘制代码复制到新 UI，而是复用以下内容：

- 旧实现中已经验证过的 API 命令、参数、返回值解析、错误码和状态机。
- `cp0_lvgl` 提供的 C API、`cp0_signal_*` API、异步 sudo/系统管理 API 及其生命周期约束。
- 新 UI 已完成的页面组件、滚轮导航、输入框、动画和异步任务框架。

最终验收以 APPLaunch 集成版本为准；`projects/ui_test` 只是新 UI 的独立验证工程。

## 2. 代码现状与关键结论

### 2.1 目录关系

当前仓库实际情况如下：

- 旧 Settings UI：`/home/nihao/work/launcher/projects/APPLaunch/legacy/settings`
- 新 Settings UI 的真实源码：`/home/nihao/work/launcher/projects/ui_test/main/ui`
- APPLaunch 使用的新 Settings UI 路径：`/home/nihao/work/launcher/projects/APPLaunch/main/ui/settings`
- `APPLaunch/main/ui/settings` 当前是指向 `../../../ui_test/main/ui` 的符号链接，不是独立源码副本。
- 后端 API：`/home/nihao/work/launcher/ext_components/cp0_lvgl`

因此，任何新 Settings UI 的源码修改都应先明确修改真实路径 `projects/ui_test/main/ui`，不要把符号链接当成新的源码目录单独维护。

### 2.2 新 UI 当前结构

主要文件和职责：

- `settings_page.cpp`：创建 Settings 树、注册页面 factory、集中放置 Wi‑Fi/Bluetooth 开关回调和 Launcher 动态菜单。
- `settings_page_api.cpp`：当前只包含 ADB guide 的占位回调、`mork_api` 占位回调和 Launcher 开关回调。
- `settings_menu_roller.hpp`：一级/二级滚轮和页面切换。
- `settings_tree_types.hpp`：树节点、页面 factory、组件 API 回调和 `PageType` 定义。
- `lvgl_components.hpp`：新 UI 的基础组件和数值页基类。
- `settings_wifi_page.hpp`：Wi‑Fi 扫描、连接、隐藏网络和密码输入，目前仍使用 Mock 数据及 Mock 后端逻辑。
- `settings_bluetooth_page.hpp`：Bluetooth 状态、连接设备、扫描和别名编辑，已经使用异步 API 调度框架，但需要逐项对照真实命令和返回格式。
- `settings_sound_card_page.hpp`、`settings_sound_card_detail_page.hpp`：声卡/混音器页面，目前使用 Mock 声卡和 Mock 控件。
- `settings_brightness_page.hpp`、`settings_volume_page.hpp`、`settings_camera_resolution_page.hpp`、`settings_screen_timeout_page.hpp`、`settings_battery_calibration_page.hpp`、`settings_rtc_page.hpp`：数值类页面，页面结构已有，但业务动作仍需接入旧接口。

### 2.3 旧 UI 的业务分组

旧实现的业务入口主要分布如下：

- `legacy/settings/main/ui/page_app/setting/basic.cpp`：Launcher、Boot、Brightness、DarkTime、Volume、Camera Resolution。
- `legacy/settings/main/ui/page_app/setting/wifi.cpp`、`wifi_view.cpp`：Wi‑Fi 状态、扫描、连接、隐藏网络、忘记网络。
- `legacy/settings/main/ui/page_app/setting/bluetooth.cpp`、`bluetooth_service.cpp`、`bluetooth_view.cpp`：Bluetooth 电源、扫描、已连接设备、配对/连接/删除、别名。
- `legacy/settings/main/ui/page_app/setting/sound_card.cpp`、`sound_card_view.cpp`：声卡列表、控件列表、控件详情和写入。
- `legacy/settings/main/ui/page_app/setting/developer.cpp`、`developer_view.cpp`：ADB 状态、ADB 开关、授权清理、配对流程、USB guide。
- `legacy/settings/main/ui/page_app/setting/rtc.cpp`、`rtc_view.cpp`：NTP、时间读取、时间设置、写 RTC 确认。
- `legacy/settings/main/ui/page_app/setting/info.cpp`：电池信息和 BQ27220 校准。
- `legacy/settings/main/ui/page_app/setting/system.cpp`：网络信息、账户信息、版本/构建信息、系统更新。
- `legacy/settings/main/ui/page_app/ui_app_setup.cpp`：旧 UI 共享的 Config、GPIO、Audio、确认框和资源路径接口。

### 2.4 当前新 UI 的明确缺口

以下内容不能直接认为已经接好了：

1. Wi‑Fi 页面在 `settings_wifi_page.hpp` 中包含 `MockAccessPoint`、`MockError`、`initialize_mock_data()`、`mock_networks_` 和 `mock_connected_ssid_`，必须替换为真实 `cp0_wifi_*`/`cp0_signal_wifi_api` 调用。
2. 声卡页面包含 `mock_cards()`、`mock_controls()` 以及 `Applied (mock)`，必须替换为 `cp0_signal_soundcard_api`。
3. `settings_page.cpp` 中 `mork_api` 仍是占位逻辑，当前被用于 NTP、ADB、扩展端口及非 Launcher 构建下的若干开关，不能保留到正式集成版本。
4. `LvSettingValuePage3Base::activate_selected()` 在调用组件 API 后无条件执行 `LeaveSelfPage()`。如果业务操作改成异步，页面可能在后端回调前被销毁，必须由基础设施任务统一修正或由各组件显式接管提交生命周期。
5. 后端回调不应直接操作 LVGL。新 Bluetooth 页面已经采用“后端回调入队、LVGL timer 消费”的方向；Wi‑Fi、声卡和所有新增页面必须遵守同样的规则。
6. `SettingApiCallBackFunc` 的读取/激活接口是同步形态，而真实 Bluetooth、sudo、系统管理操作可能异步。不能把异步调用伪装成同步返回；应让页面状态保持 pending，并由 LVGL 线程完成结果提交。
7. 当前 Settings 树中 Ethernet、Account、Update、About、Help、Info 的若干条目还是静态标签或空页面，需要决定是本次迁移完整接入，还是明确标为后续任务；正式验收不能出现“能点进去但没有业务”的条目。

## 3. 统一实现规则

### 3.1 API 调用规则

1. 优先使用 `cp0_lvgl` 已公开的强类型 C API：Wi‑Fi、Bluetooth、Battery、Backlight、Volume、Time、Process 等。
2. 需要字符串命令/结构化 payload 时使用已有 `cp0_signal_*` API，不在 UI 层直接执行 `nmcli`、`amixer`、`bluetoothctl`、`sudo` 或 shell。
3. 需要权限的操作必须使用 `cp0_signal_system_admin_async`、`cp0_signal_sudo_argv_async` 或后端规定的异步接口；禁止重新使用已标记 deprecated 的同步 root 调用。
4. 所有命令参数必须符合后端 contract：命令名、参数个数、数值范围、布尔值格式、字符串长度和控制字符限制均按后端实现执行。
5. 返回值必须同时检查 `code` 和 `data`：`code == 0` 不代表 payload 一定可解析，解析失败必须显示错误并回滚 UI 状态。
6. 对可恢复错误显示用户可理解的提示；对 `RADIO_OFF`、`AUTH`、`NOT_FOUND`、`IP_CONFIG`、`TIMEOUT`、`SERVICE` 等错误保留后端错误语义。

### 3.2 异步与 LVGL 线程规则

每个异步页面必须具备以下生命周期：

1. 创建页面时建立 `lifetime token`、任务注册器或等价的 owner 状态。
2. 后端回调只做轻量数据复制和结果入队，不访问任何 LVGL 对象。
3. 通过 `lv_timer`、`lv_async_call` 或现有 dispatch 队列在 LVGL 线程消费结果。
4. 消费结果前检查页面仍存活、generation/token 未变化、当前操作仍是该请求。
5. 页面析构时按顺序停止扫描/取消请求、阻止新回调入队、清理 timer/event descriptor、等待任务结束，最后删除 LVGL 对象。
6. 同一类操作必须有 pending 防抖，禁止连续 Enter 产生多个并发写操作。
7. 异步动作完成前页面不能无条件返回；成功、失败、取消三种路径都必须明确恢复焦点和页面状态。

### 3.3 合并规则

- 组件开发者不得修改其他组件的页面文件。
- 不得直接修改 `settings_page.cpp` 的中央 Settings 树，除非任务书明确分配给中央集成负责人。
- 组件与中央树之间通过独立 factory、adapter 或公开函数交付，避免所有人争抢同一文件。
- 不把旧 `legacy/settings` 目录重新加入 APPLaunch 的 `SRCS`；旧实现只作为业务参考和纯模型/contract 复用来源。
- 不修改 `ext_components/cp0_lvgl` 后端实现来迁就 UI，除非确认后端 contract 本身缺陷，并由后端负责人单独立项、补测试后再合并。

## 4. 团队角色与并行任务

建议安排 12 名程序员。每个任务均在独立 `ui_testN` 工作目录中完成并编译通过，最后由总工按任务顺序合并。

### T00：总工/架构与合并负责人

**职责**

- 维护中央 Settings 树、最终 APPLaunch 接入和合并顺序。
- 建立 API 命令/返回格式登记表，裁决组件之间的公共接口。
- 维护集成基线、合并分支、冲突解决和最终验收。

**主要文件范围**

- `projects/APPLaunch/main/ui/builtin_app_registry.cpp`
- `projects/ui_test/main/ui/settings_page.cpp`
- `projects/ui_test/main/ui/settings_page.hpp`
- `projects/ui_test/main/ui/settings_page_api.cpp`
- 必要时新增 `projects/ui_test/main/ui/settings_integration_*.{hpp,cpp}`

**不得做的事**

- 不在中央文件中直接实现所有组件业务。
- 不接受未独立编译、未说明 API contract 和未提供测试记录的组件提交。

**交付物**

- 集成基线分支。
- Settings 树最终节点表。
- API 适配登记表。
- 每次合并的冲突记录、回归结果和已知问题清单。

### T01：公共异步 API/LVGL 生命周期基础设施

**目标**

解决新 UI 的同步组件 API 与真实异步后端之间的生命周期冲突，供其他任务复用。

**工作范围**

- `settings_tree_types.hpp`：补充统一的异步操作状态/结果定义，或提供不破坏现有调用方式的 adapter。
- `settings_menu_roller.hpp`：修正开关状态读取、激活后刷新和 pending 状态显示机制。
- `lvgl_components.hpp`：修正数值页提交后无条件退出的问题，允许异步 page factory 自己决定何时返回。
- 新增独立的 `settings_async_dispatch.hpp/.cpp` 或等价公共组件。

**必须验证**

- 页面销毁后后端回调不会触碰 LVGL。
- 同一请求重复触发会被抑制。
- 成功/失败/取消都能恢复焦点。
- `lv_async_call` 失败、timer 已删除、页面被提前退出时不泄漏数据。

**依赖**：无。其他页面任务应先读取 T01 的接口约定，再编写业务。

### T02：构建、运行入口和目录隔离

**目标**

保证每个开发者都能在独立副本中编译，不污染主工程和其他开发者目录。

**工作范围**

- 复制 `projects/ui_test` 为独立 worker 工程并验证 `SConstruct`。
- 确认 `APPLaunch/main/ui/settings` 符号链接在复制后不会指向错误目标。
- 维护 worker 工程的 `config_defaults.mk`、静态资源和 build 输出隔离规则。
- 提供 SDL 本机构建和 CP0 交叉构建命令。

**交付物**

- worker 工程模板。
- 一键构建说明。
- 构建产物不得写入主工程 `build` 目录。

**依赖**：无；建议与 T01 并行。

### T03：Wi‑Fi

**主要文件范围**

- `settings_wifi_page.hpp`
- 新增 `settings_wifi_api.hpp/.cpp`、`settings_wifi_model.hpp/.cpp` 或测试文件。

**需要替换的内容**

- 删除 Mock AP、Mock 连接状态和 Mock 错误路径。
- 电源状态使用 `RadioEnabled`/`RadioSetEnabled` 或对应强类型 C API。
- 扫描使用 `Scan`，解析 `ssid:signal:security:in_use:saved` 行格式。
- 连接分别使用 `Connect` 和 `ConnectHidden`。
- 忘记网络使用 `ProfileForget`，需要时调用 `ProfileDisconnectActive`。
- 页面标题使用 `Status` payload：`connected:ssid:ip:signal:ethernet`。

**必须保留的旧行为**

- Wi‑Fi 关闭时进入扫描页面显示提示。
- 开放网络、已保存网络、输入密码网络、隐藏网络四种连接来源。
- 认证失败、网络不存在、IP 配置失败、超时和服务不可用提示。
- 扫描/连接/退出时停止任务，页面删除后不回调已失效对象。

**验收场景**

- 关闭 Wi‑Fi进入 Scan；打开后重新扫描。
- 扫描列表显示真实 SSID、信号、加密类型、当前连接和已保存标志。
- 连接成功后标题显示真实 SSID/IP。
- 密码错误不误报成功，退出连接中页面不会崩溃。
- 忘记当前网络后状态正确刷新。

### T04：Bluetooth

**主要文件范围**

- `settings_bluetooth_page.hpp`
- `settings_bluetooth_connected_devices_page.hpp`
- `settings_bluetooth_scan_page.hpp`
- 新增 Bluetooth adapter/decoder 测试。

**API 范围**

- 状态：`BtStatus` 或 session 版 `BtSessionInit`/`BtStatusGet`。
- 电源：`BtPower 0|1`。
- 可发现：`BtDiscoverable 0|1`。
- 别名：`BtAlias <text>`，不得含控制字符，长度小于 64 字节。
- 扫描：`BtScan <max>` 或 session 版 `BtScanOn`/`BtScanOff`。
- 列表：`BtList <max>`、`BtConnectedList <max>`。
- 设备操作：`BtPair`、`BtConnect`、`BtDisconnect`、`BtRemove`。

**重点任务**

- 核对当前页面使用的命令名与 backend contract 完全一致。
- 对 `Status` 的制表符字段和设备列表字段做严格解析。
- 将后端回调统一转到 LVGL timer/dispatch 队列。
- 电源关闭时阻止 Discoverable、Connected、Scan，并显示提示。
- session 创建/销毁、扫描启停和页面析构必须成对。

**验收场景**

- 电源读取、开关回滚、Discoverable gating。
- 已连接设备列表和扫描列表分别可用。
- 配对/连接/断开/删除成功与失败均有反馈。
- 别名输入支持 UTF‑8 光标、删除、保存和失败回滚。

### T05：Audio 音量和系统声音

**主要文件范围**

- `settings_volume_page.hpp`
- 新增 `settings_audio_api.hpp/.cpp` 或适配层。

**API 范围**

- `VolumeRead`：返回 `0..100`。
- `VolumeWrite <0..100>`：提交后解析并显示实际返回值。
- 系统声音预览使用 `SystemSoundPlay` 或 `PlayFile`，遵循音频 contract。
- 如需要系统声音开关，使用 `SystemSoundEnable`，不得自行保存 UI 假状态。

**重点任务**

- 按旧 `UISetupPage::audio_volume_read/write` 的语义保留范围校验和失败回滚。
- 处理滑轮选项值与后端百分比值的映射。
- 提交期间禁止重复写入；成功后再退出或刷新。
- 不在音频 callback 中操作 LVGL。

### T06：SoundCard/ALSA Mixer

**主要文件范围**

- `settings_sound_card_page.hpp`
- `settings_sound_card_detail_page.hpp`
- 新增解析/adapter 测试。

**需要替换的内容**

- 删除 `mock_cards()`、`mock_controls()`、`Applied (mock)`。
- `ListCards` 返回内容按 `cp0_alsa_parser` 规则解析。
- `ListControls <card_index>` 获取控件列表。
- `GetControlDetail <card_index> <control>` 获取当前值和范围。
- `SetControl <card_index> <control> <value>` 写入真实控件。

**重点任务**

- 复用旧 `SoundCardModel` 的解析、clamp 和控制类型语义。
- 后端命令必须通过 `cp0_signal_soundcard_api`，不能由 UI 直接调用 `amixer`。
- 处理无声卡、无控件、控件不可写和写入失败。
- 切换 card/control 时取消前一个请求，避免旧结果覆盖新页面。

### T07：Screen/Backlight/DarkTime

**主要文件范围**

- `settings_brightness_page.hpp`
- `settings_screen_timeout_page.hpp`
- 新增屏幕设置 adapter。

**API 范围**

- `BacklightRead`、`BacklightMax`、`BacklightWrite`。
- DarkTime 复用旧配置键和 `config` API；键名、默认值和保存行为以旧实现为准。
- 配置写入使用 `GetInt`、`SetInt`、`Save`，不得只改内存状态。

**重点任务**

- 复用旧 `setup_value_policy` 的非负数、范围和默认值规则。
- 处理设备 backlight 最大值不是 100 的情况，做好百分比与原始值换算。
- 写入失败时回滚选中项。
- DarkTime 的 Never/10S/30S/60S/300S 映射到旧配置值并保存。

### T08：Camera Resolution

**主要文件范围**

- `settings_camera_resolution_page.hpp`
- 新增 camera/config adapter 和解析测试。

**工作范围**

- 读取旧配置键 `camera.resolution.width`、`camera.resolution.height`。
- 维持旧实现的宽高成对写入和 `Save` 顺序。
- 校验 `1280x720`、`640x480` 等页面选项；对后端不支持的分辨率显示失败并恢复旧值。
- 如需要调用 `cp0_signal_camera_api`，先按 camera contract 确认命令和 payload，不自行猜测。

### T09：Battery/Info/BQ27220

**主要文件范围**

- `settings_battery_calibration_page.hpp`
- 新增 Info/Battery 页面或 adapter。

**API 范围**

- 电池读取：`cp0_battery_read()` 或 `cp0_signal_bq27220_api({"Read"})`。
- 校准：`Calibrate 0..3`，分别对应 Enter CAL、CC Offset、Board Offset、Exit CAL。

**重点任务**

- 复用旧 `Info` 和 `setup_info_model` 的字段格式化、有效性检查和刷新周期。
- 电压、电流、温度、SOC 等值显示真实数据，读取失败不得显示上一帧为当前值。
- 校准操作必须 pending、防重复、显示结果，并在页面返回前确保回调不再访问页面。

### T10：RTC/NTP

**主要文件范围**

- `settings_rtc_page.hpp`
- 新增 `settings_rtc_api.hpp/.cpp` 和模型测试。

**API 范围**

- NTP：`NtpGet`、`NtpSet 0|1`。
- 系统时间读取：按旧 `osinfo` API 或 `cp0_time_str`。
- 写时间：`TimeSet <timestamp>`。
- 写硬件 RTC 前保留确认页面和 NTP 关闭限制。

**重点任务**

- 复用旧 `RtcStateModel` 的年月日时分秒范围和闰年/天数校验。
- 按旧实现的顺序执行：编辑 → 确认 → 写系统时间/硬件 RTC。
- NTP 开启时禁止或提示手动写 RTC。
- 处理跨午夜、非法日期、时间设置失败和取消。

### T11：Developer/ADB

**主要文件范围**

- `settings_adb_guide_page.hpp`
- 新增 `settings_adb_api.hpp/.cpp` 或等价 adapter；`settings_page_api.cpp` 和中央树绑定由 T00 合并。
- 新增 ADB 状态模型/解析测试。

**API 范围**

- ADB 状态：`cp0_signal_process_api({"AdbStatus"})`。
- ADB 开关和授权清理：`cp0_signal_system_admin_async`，按旧 `Developer` 的超时、取消和结果码处理。
- 需要取消时调用 `cp0_signal_sudo_cancel`。

**重点任务**

- 复用旧 `adb_state` 的输出解析和公钥/授权校验。
- ADB guide 的“现在重启/稍后”动作必须接到真实 reboot API。
- sudo 回调选择正确线程；输出回调快速返回，不在其中更新 LVGL。
- 处理认证失败、执行失败、取消、超时和退出时取消请求。

### T12：System/Launcher/Boot/ExtPort/About/Help

本任务可由两名程序员拆成 T12A 和 T12B；如人员充足，必须保持文件范围不重叠。

#### T12A：System/Account/Ethernet/Update

**工作范围**

- 新增 Ethernet、Account、Update 页面或 adapter。
- 使用旧 `system.cpp` 的 API 和解析逻辑：`NetworkDefaultInfoRead`、`EthInfoRead`、`AccountInfoRead`、`NtpGet`、`AptUpdateBackground`、`UpdateLauncherBackground` 等。
- 复用 `system_page_model` 的 payload 解析、版本/构建标签和更新状态文字。
- 更新任务必须显示进行中、成功、失败、超时，页面退出时停止 timer/取消任务。

#### T12B：Launcher/Boot/ExtPort/About/Help

**工作范围**

- Launcher 动态 app 开关复用 `launcher_app_registry_is_enabled/set_enabled/notify_changed`。
- Boot 的 Reboot/Shutdown 通过 `cp0_signal_process_api`，需要 factory reset 时按旧 `boot_action_policy` 的操作顺序执行。
- ExtPort 使用 `GpioGet/GpioSet`，保留 active-low、失败回滚和状态刷新。
- About/Help/Info 的静态条目必须明确是静态信息还是接真实数据，不得留下无动作空节点。

**注意**

- Launcher factory 当前会动态重建子树，这是中央树生命周期的一部分；T12B 只能提供回调和 adapter，不直接修改 `settings_page.cpp`。

## 5. API 适配登记表

总工在合并前维护下表，每个组件必须补齐“请求、回调线程、payload、错误码、取消方式、页面行为”。

| 组件 | 请求/接口 | 成功结果 | 失败/特殊情况 | UI 处理 |
|---|---|---|---|---|
| Wi‑Fi | `RadioEnabled`, `RadioSetEnabled`, `Status`, `Scan`, `Connect`, `ConnectHidden`, `ProfileForget` | code、status/scan payload | `-2..-7` Wi‑Fi 错误码 | pending、错误提示、回滚、停止扫描 |
| Bluetooth | `BtStatus`, `BtPower`, `BtDiscoverable`, `BtAlias`, `BtList`, `BtConnectedList`, `BtScan` | 制表符状态/设备记录 | invalid session、蓝牙后端错误 | dispatch 到 LVGL、session 成对销毁 |
| Volume | `VolumeRead`, `VolumeWrite` | `0..100` | invalid request、后端失败 | 范围校验、实际值刷新 |
| Backlight | `BacklightRead`, `BacklightMax`, `BacklightWrite` | 原始亮度值 | 无效范围、硬件失败 | 百分比换算、失败回滚 |
| SoundCard | `ListCards`, `ListControls`, `GetControlDetail`, `SetControl` | ALSA parser payload | 无声卡、控件不可写 | 取消旧请求、刷新真实值 |
| Camera | config API、camera API | 宽高/状态 | 不支持分辨率、配置失败 | 成对写入、保存、回滚 |
| Battery | `Read`, `Calibrate 0..3` | 电池编码/校准 code | invalid command、reader unavailable | 周期刷新、pending、结果反馈 |
| RTC | `NtpGet`, `NtpSet`, `TimeSet`、OS info | 状态/时间 | NTP 限制、非法时间 | 确认后提交、错误回滚 |
| ADB | `AdbStatus`、system admin async | status/complete code | auth、exec、cancel、timeout | 不阻塞 UI、可取消 |
| System | OS info/update APIs | info payload/job state | service、timeout | 状态页、timer、退出清理 |
| ExtPort | `GpioGet`, `GpioSet` | 0/1 | GPIO 无效、硬件失败 | active-low 转换、回滚 |

## 6. 每个 worker 的目录和操作协议

用户给出的路径字符串中出现了重复前缀。为避免实际路径变成“工程目录下再嵌套一套绝对路径”，统一采用下面的规范化目录：

- `ui_test1`：`/home/nihao/w2T/github/launcher/projects/ui_test1`
- `ui_test2`：`/home/nihao/w2T/github/launcher/projects/ui_test2`
- `ui_test3`：`/home/nihao/w2T/github/launcher/projects/ui_test3`
- 依此类推。

如果团队流程确实要求保留一个公共母目录，则母目录为 `/home/nihao/w2T/github/launcher/projects/ui_test`，worker 目录应是 `/home/nihao/w2T/github/launcher/projects/ui_test/ui_test1`，而不是把 `/home/nihao/w2T/github/launcher/projects/ui_test` 再拼接一次。

### 6.1 创建 worker 副本

建议由总工或脚本统一执行：

```sh
cd /home/nihao/w2T/github/launcher/projects
cp -a ui_test ui_test1
cp -a ui_test ui_test2
```

每个 worker 必须确认：

```sh
cd /home/nihao/w2T/github/launcher/projects/ui_test1
pwd
readlink -f main/ui
git status --short
```

如果副本内的 `main/ui`、资源或构建目录仍指向其他 worker，必须先修正；不同 worker 不得共享 `build`、`.cache` 或生成配置。

### 6.2 worker 修改边界

- T03 只改 Wi‑Fi 页面和 Wi‑Fi 新增 adapter/测试。
- T04 只改 Bluetooth 页面和 Bluetooth 新增 adapter/测试。
- T05 只改音量/系统声音页面和 Audio adapter/测试。
- T06 只改 SoundCard 两个页面及声卡 adapter/测试。
- T07 只改亮度/DarkTime 页面及屏幕 adapter/测试。
- T08 只改 Camera 页面及 camera/config adapter/测试。
- T09 只改 Battery/Info 页面及电池 adapter/测试。
- T10 只改 RTC 页面及 RTC adapter/测试。
- T11 只改 ADB 页面及 ADB adapter/测试。
- T12 只改 System/Launcher/Boot/ExtPort/About/Help 对应新增页面或 adapter，不直接改中央 Settings 树。
- `settings_page.cpp`、`settings_page.hpp`、`settings_page_api.cpp`、`builtin_app_registry.cpp` 默认只允许 T00 修改。

### 6.3 worker 编译要求

每个人至少完成一次 SDL 本机构建：

```sh
cd /home/nihao/w2T/github/launcher/projects/ui_test1
export CONFIG_DEFAULT_FILE=linux_x86_sdl2_config_defaults.mk
scons -j8
```

涉及交叉工具链的 worker 还必须完成 CP0 交叉构建：

```sh
cd /home/nihao/w2T/github/launcher/projects/ui_test1
export CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk
scons -j8
```

后端 contract 相关改动需要额外执行：

```sh
cd /home/nihao/w2T/github/launcher/ext_components/cp0_lvgl
./tests/run_tests.sh
```

若测试或构建失败，worker 必须在交付记录中写明：命令、完整错误摘要、是否为环境/工具链问题、是否已验证与本任务无关。

## 7. 推荐开发和合并顺序

### 阶段 A：先建立公共规则

1. T00 建立集成基线和 API 登记表。
2. T01 完成异步 dispatch、页面生命周期和数值页提交约定。
3. T02 固化 worker 副本及 SDL/交叉编译脚本。

### 阶段 B：先接入独立、风险较低的页面

并行执行 T05、T07、T08、T09、T10。它们可以先验证“读值 → 编辑 → 提交 → 失败回滚”的公共模型。

### 阶段 C：接入复杂异步页面

并行执行 T03、T04、T06、T11。重点验证扫描、sudo、后台回调、页面退出和取消。

### 阶段 D：补齐系统信息和中央树

执行 T12A、T12B；随后由 T00 将所有 factory/adapter 接入 `settings_page.cpp`，删除 `mork_api`，清理静态空节点。

### 阶段 E：APPLaunch 总体验收

1. 在 `projects/ui_test` 验证独立 Settings 工程。
2. 在 `projects/APPLaunch` 验证 `main/ui/settings` 符号链接和 `LAUNCHER_BUILD` 条件编译。
3. 进入 APPLaunch 的 Settings app，按组件验收表逐项操作。
4. 进行页面快速进出、连续 Enter、异步操作中按 ESC、设备不可用和后端超时回归。

## 8. 总工合并门禁

每个 worker 交付前必须提供以下信息：

- 修改文件清单，确保没有越过任务边界。
- API 请求列表及对应 backend contract 文件。
- 异步回调线程和 LVGL 线程切换方式。
- 页面析构时的取消、token/generation 和任务 join 方案。
- SDL 构建命令及结果。
- 适用时的 CP0 交叉构建命令及结果。
- 新增/执行的单元测试、模型测试或手工验收步骤。
- 已知限制和未完成项。

总工拒绝合并以下提交：

- 保留 Mock 数据或 `mork_api` 占位逻辑。
- 在后端线程直接调用 `lv_*`。
- 通过直接 shell 命令绕过 `cp0_lvgl` API。
- 异步请求没有取消/销毁处理。
- 页面操作失败后 UI 状态没有回滚。
- 只在 `projects/ui_test` 编译通过，但 `APPLaunch` 的 `LAUNCHER_BUILD` 不通过。
- 修改了别人的组件文件或把中央树改动混入组件提交。

## 9. 最终验收清单

### 功能

- Launcher app 开关、Boot 重启/关机、ExtPort 开关可用。
- Brightness、DarkTime、Volume、Camera Resolution 可读取、修改、保存和失败回滚。
- Wi‑Fi 电源、扫描、连接、隐藏网络、密码、忘记网络可用。
- Bluetooth 电源、Discoverable、别名、已连接设备、扫描、配对/连接/删除可用。
- SoundCard 能读取真实声卡、控件、范围并写入真实值。
- Battery/Info 显示真实数据，BQ27220 四个校准动作可用。
- RTC/NTP 能完成读取、编辑、确认、写入和错误提示。
- ADB 状态、开关、授权及 guide/reboot 流程可用。
- Ethernet、Account、Update、About、Help 不存在无业务空节点。

### 稳定性

- 所有异步页面反复进入/退出不崩溃、不 UAF、不泄漏线程。
- 扫描、连接、sudo、更新过程中按 ESC 可安全退出或取消。
- 连续快速按 Enter 不会产生重复请求。
- 后端不可用、权限失败、超时、非法 payload 都能回到可操作状态。
- SDL 本机和 CP0 交叉构建均通过。

### 代码质量

- 旧 UI 页面不重新加入活动构建。
- Mock 和占位 API 全部移除或显式隔离在测试代码中。
- API 解析优先复用 `cp0_lvgl` contract/旧模型测试，不复制一套不一致的协议。
- 每个组件有清晰的 adapter、页面和测试边界。
- 总工能够在不修改组件内部代码的情况下，通过 factory/adapter 完成中央树合并。
