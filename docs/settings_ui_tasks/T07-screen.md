# T07：Screen/Backlight/DarkTime

**固定 worker 目录**：`/home/nihao/w2T/github/launcher/projects/ui_test7`

## 目标

接入亮度和屏幕超时真实接口，保留旧配置键、范围和保存规则。

## 负责文件

- `settings_brightness_page.hpp`
- `settings_screen_timeout_page.hpp`
- 新增屏幕设置 adapter/测试。

## API

- `BacklightRead`
- `BacklightMax`
- `BacklightWrite value`
- DarkTime 使用 `config` API 的 `GetInt`、`SetInt`、`Save`。

## 工作内容

1. 复用旧 `setup_value_policy` 的非负数和范围检查。
2. 处理 backlight 最大值不是 100 的设备，完成百分比/原始值换算。
3. Brightness 写入失败时回滚选中项。
4. 将 Never、10S、30S、60S、300S 映射到旧 DarkTime 配置值。
5. 配置必须持久化，不能只修改内存。

## 验收

- 读取真实亮度并正确选中页面项。
- 0%、25%、50%、75%、100% 映射正确。
- DarkTime 每个选项读写正确。
- 后端失败、非法值、保存失败均有回滚或提示。
