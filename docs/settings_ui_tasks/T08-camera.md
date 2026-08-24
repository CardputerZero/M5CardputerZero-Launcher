# T08：Camera Resolution

**固定 worker 目录**：`/home/nihao/w2T/github/launcher/projects/ui_test8`

## 目标

将新 Camera Resolution 页面接入旧配置和 camera backend，确保分辨率成对读写并持久化。

## 负责文件

- `settings_camera_resolution_page.hpp`
- 新增 camera/config adapter、解析测试。

## API 与配置

- `camera.resolution.width`
- `camera.resolution.height`
- `GetInt`、`SetInt`、`Save`
- 如使用 `cp0_signal_camera_api`，必须以 camera contract 为准，不猜测命令。

## 工作内容

1. 读取 width/height 并映射到页面选项。
2. 维持旧实现的宽高成对写入顺序。
3. 写入两个字段后调用 `Save`。
4. 后端不支持分辨率时显示失败并恢复旧值。
5. 处理读取缺失、非法尺寸和保存失败。

## 验收

- `1280x720`、`640x480` 可正确切换。
- 只写入一半配置不会发生。
- 退出/重复进入页面后读取到持久化值。
- camera/config backend 错误有明确提示。
