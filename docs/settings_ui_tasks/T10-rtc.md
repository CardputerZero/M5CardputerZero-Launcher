# T10：RTC/NTP

**固定 worker 目录**：`/home/nihao/w2T/github/launcher/projects/ui_test10`

## 目标

将新 RTC 页面接入 NTP、系统时间和硬件 RTC 写入接口，保留旧 UI 的范围校验和确认流程。

## 负责文件

- `settings_rtc_page.hpp`
- 新增 `settings_rtc_api.hpp/.cpp` 和模型测试。

## API

- `NtpGet`
- `NtpSet 0|1`
- `TimeSet timestamp`
- 旧 `osinfo` API 或 `cp0_time_str` 读取当前时间。

## 工作内容

1. 复用旧 `RtcStateModel` 的年月日时分秒范围、闰年和月份天数校验。
2. 保持“编辑 → 确认 → 写系统时间/硬件 RTC”的顺序。
3. NTP 开启时禁止或提示手动写 RTC。
4. 处理跨午夜、非法日期、时间设置失败和取消。
5. 写入期间保持页面 pending，成功后再退出或刷新。

## 验收

- NTP 开关读取和设置正确。
- 年月日时分秒范围正确。
- 非法日期不会发送 backend 请求。
- 确认 Yes/No、写入成功/失败、按 ESC 均安全。
