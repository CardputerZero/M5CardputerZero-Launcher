# T12A：System/Account/Ethernet/Update

**固定 worker 目录**：`/home/nihao/w2T/github/launcher/projects/ui_test12`

## 目标

补齐新 Settings 树中的系统信息、账户、Ethernet 和更新功能，消除“能进入但没有业务”的静态空页面。

## 负责范围

- 新增 System/Account/Ethernet/Update 页面或 adapter。
- 新增 `settings_system_*.{hpp,cpp}`、model 和测试。

不得修改中央 Settings 树；factory 绑定由 T00 完成。

## API

- `NetworkDefaultInfoRead`
- `EthInfoRead`
- `AccountInfoRead`
- `AptUpdateBackground`
- `UpdateLauncherBackground`
- 版本、构建日期、commit 使用 APPLaunch 已有编译宏和旧 model。

## 工作内容

1. 复用旧 `system_page_model` 的 payload 解析和标签格式化。
2. 显示 Ethernet IP、Gateway、MAC。
3. 显示 Account username/hostname；对不支持的 Password 明确提示。
4. 更新操作显示进行中、成功、失败、超时和取消状态。
5. 更新任务页面退出时停止 timer、取消或忽略旧任务结果。

## 验收

- 网络、账户、版本和构建信息显示真实数据。
- 更新动作不会阻塞 LVGL。
- 后端不可用、超时、失败时页面仍可操作。
- 不保留静态空节点。
