# T05：Audio 音量与系统声音

**固定 worker 目录**：`/home/nihao/w2T/github/launcher/projects/ui_test5`

## 目标

将 `settings_volume_page.hpp` 接入真实音频 API，保留旧 UI 的范围校验、预览和失败回滚。

## 负责文件

- `settings_volume_page.hpp`
- 新增 `settings_audio_api.hpp/.cpp` 或等价 adapter/测试。

## API

- `VolumeRead`，返回 `0..100`。
- `VolumeWrite 0..100`。
- 系统声音预览使用 `SystemSoundPlay` 或 `PlayFile`。
- 系统声音开关使用 `SystemSoundEnable`，不得维护 UI 假状态。

## 工作内容

1. 复用旧 `UISetupPage::audio_volume_read/write` 语义。
2. 将滚轮值映射为 backend 百分比值。
3. 提交期间禁止重复写入。
4. 成功后显示 backend 实际值；失败后恢复原值。
5. 预览操作必须使用音频 API，不直接访问文件或播放器。

## 异步要求

- 不在 audio callback 中操作 LVGL。
- 页面退出前取消或忽略旧请求。
- 预览和写入不能互相覆盖状态。

## 验收

- 读取、修改、保存 0%、中间值、100%。
- 后端拒绝写入时 UI 回滚。
- 连续 Enter 不重复发送请求。
- SDL 和 CP0 配置均能编译。
