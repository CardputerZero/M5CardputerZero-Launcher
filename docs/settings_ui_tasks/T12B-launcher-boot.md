# T12B：Launcher/Boot/ExtPort/About/Help

**固定 worker 目录**：`/home/nihao/w2T/github/launcher/projects/ui_test13`

## 目标

接入 Launcher 动态开关、Boot 动作、扩展端口，并明确 About/Help 的业务或静态信息行为。

## 负责范围

- 新增 Launcher/Boot/ExtPort/About/Help 页面或 adapter。
- 复用旧 `boot_action_policy`、`setup_value_policy`、launcher registry 接口。

不得直接修改中央 Settings 树；T00 负责 factory 和节点绑定。

## API

- Launcher：`launcher_app_registry_is_enabled`、`launcher_app_registry_set_enabled`、`launcher_app_registry_notify_changed`。
- Boot：`cp0_signal_process_api({"Reboot"})`、`cp0_signal_process_api({"Shutdown"})`。
- ExtPort：`GpioGet`、`GpioSet`。
- Factory reset/OOBE 操作严格复用旧 `boot_action_policy` 的执行顺序。

## 工作内容

1. Launcher 只显示 `configurable` app，并保持 registry 状态同步。
2. Launcher 动态子树创建和销毁不能泄漏迭代器或回调。
3. Reboot/Shutdown 必须经过确认页。
4. ExtPort 保留 active-low 转换、失败回滚和状态刷新。
5. About/Help 明确实现静态信息、帮助页面或后续禁用状态，不保留无动作空节点。

## 验收

- Launcher app 开关立即生效并通知 registry。
- Reboot/Shutdown 确认 Yes/No 正确。
- ExtPort 读写真实 GPIO，失败时恢复原状态。
- Settings 反复进入退出不影响 Launcher 主页面。
