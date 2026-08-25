# T11：Developer/ADB

**固定 worker 目录**：`/home/nihao/w2T/github/launcher/projects/ui_test11`

## 目标

将新 Developer/ADB 页面接入真实 ADB 状态、管理员操作、授权清理和 USB guide/reboot 流程。

## 负责文件

- `settings_adb_guide_page.hpp`
- 新增 `settings_adb_api.hpp/.cpp`、ADB 状态模型和测试。

不要直接修改 `settings_page.cpp` 或 `settings_page_api.cpp`；中央绑定由 T00 完成。

## API

- `cp0_signal_process_api({"AdbStatus"})`
- `cp0_signal_system_admin_async`
- 必要时使用 `cp0_signal_sudo_cancel`
- guide 的 reboot 使用后端规定的 `Reboot` API。

## 工作内容

1. 复用旧 `adb_state` 的状态输出解析和授权校验。
2. 接入 ADB enable/disable、授权清理和配对流程。
3. 按旧实现处理 sudo 超时、取消、认证失败和执行失败。
4. guide 的“现在重启/稍后”动作接入真实后端。
5. 输出 callback 快速返回，不在其中更新 LVGL。

## 验收

- ADB 状态显示 active/enabled/authorizations。
- 开关成功和失败能够回滚。
- 授权清理、配对、重启 guide 流程可用。
- 操作中按 ESC、页面析构和 sudo cancel 均安全。
