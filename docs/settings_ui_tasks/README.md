# Settings UI 分人员任务包

本目录将 Settings UI 接口迁移工作拆分为公共规范、总工任务、基础设施任务和各组件程序员任务。

## 使用顺序

1. 所有人先阅读 `00-common-rules.md`。
2. 总工阅读 `T00-chief-engineer.md`，先建立集成基线。
3. 公共基础设施负责人执行 `T01-async-lvgl-infrastructure.md`。
4. 构建/目录负责人执行 `T02-build-isolation.md`。
5. 各组件负责人领取对应的 `T03`–`T12B` 文件。
6. 所有人在自己的 `ui_testN` 副本中修改并编译；总工最后统一合并。

## 任务清单

| 任务 | 负责人 | 固定 worker 目录 | 文档 |
|---|---|---|---|
| T00 | 总工/架构与合并 | `/home/nihao/w2T/github/launcher/projects/ui_test0` | `T00-chief-engineer.md` |
| T01 | 公共异步 API/LVGL 生命周期 | `/home/nihao/w2T/github/launcher/projects/ui_test1` | `T01-async-lvgl-infrastructure.md` |
| T02 | 构建、运行入口、目录隔离 | `/home/nihao/w2T/github/launcher/projects/ui_test2` | `T02-build-isolation.md` |
| T03 | Wi‑Fi | `/home/nihao/w2T/github/launcher/projects/ui_test3` | `T03-wifi.md` |
| T04 | Bluetooth | `/home/nihao/w2T/github/launcher/projects/ui_test4` | `T04-bluetooth.md` |
| T05 | Audio 音量/系统声音 | `/home/nihao/w2T/github/launcher/projects/ui_test5` | `T05-audio-volume.md` |
| T06 | SoundCard/ALSA Mixer | `/home/nihao/w2T/github/launcher/projects/ui_test6` | `T06-soundcard.md` |
| T07 | Screen/Backlight/DarkTime | `/home/nihao/w2T/github/launcher/projects/ui_test7` | `T07-screen.md` |
| T08 | Camera Resolution | `/home/nihao/w2T/github/launcher/projects/ui_test8` | `T08-camera.md` |
| T09 | Battery/Info/BQ27220 | `/home/nihao/w2T/github/launcher/projects/ui_test9` | `T09-battery-info.md` |
| T10 | RTC/NTP | `/home/nihao/w2T/github/launcher/projects/ui_test10` | `T10-rtc.md` |
| T11 | Developer/ADB | `/home/nihao/w2T/github/launcher/projects/ui_test11` | `T11-adb.md` |
| T12A | System/Account/Ethernet/Update | `/home/nihao/w2T/github/launcher/projects/ui_test12` | `T12A-system.md` |
| T12B | Launcher/Boot/ExtPort/About/Help | `/home/nihao/w2T/github/launcher/projects/ui_test13` | `T12B-launcher-boot.md` |

## 总体代码说明

- 旧 Settings UI：`/home/nihao/work/launcher/projects/APPLaunch/legacy/settings`
- 新 Settings UI 真实源码：`/home/nihao/work/launcher/projects/ui_test/main/ui`
- APPLaunch 入口：`/home/nihao/work/launcher/projects/APPLaunch/main/ui/settings`
- 后端 API：`/home/nihao/work/launcher/ext_components/cp0_lvgl`
- `APPLaunch/main/ui/settings` 当前是指向 `../../../ui_test/main/ui` 的符号链接。
- 完整总方案仍保留在 `/home/nihao/work/launcher/docs/settings_ui_integration_task_assignment.md`。

## worker 目录规范

每名程序员已经在任务表中固定分配目录，不得自行换目录：

- T00 使用 `/home/nihao/w2T/github/launcher/projects/ui_test0`
- T01 使用 `/home/nihao/w2T/github/launcher/projects/ui_test1`
- T02 使用 `/home/nihao/w2T/github/launcher/projects/ui_test2`
- T03 使用 `/home/nihao/w2T/github/launcher/projects/ui_test3`
- T04 使用 `/home/nihao/w2T/github/launcher/projects/ui_test4`
- T05 使用 `/home/nihao/w2T/github/launcher/projects/ui_test5`
- T06 使用 `/home/nihao/w2T/github/launcher/projects/ui_test6`
- T07 使用 `/home/nihao/w2T/github/launcher/projects/ui_test7`
- T08 使用 `/home/nihao/w2T/github/launcher/projects/ui_test8`
- T09 使用 `/home/nihao/w2T/github/launcher/projects/ui_test9`
- T10 使用 `/home/nihao/w2T/github/launcher/projects/ui_test10`
- T11 使用 `/home/nihao/w2T/github/launcher/projects/ui_test11`
- T12A 使用 `/home/nihao/w2T/github/launcher/projects/ui_test12`
- T12B 使用 `/home/nihao/w2T/github/launcher/projects/ui_test13`

不要把 `/home/nihao/w2T/github/launcher/projects/ui_test` 这个完整路径再次拼接到自身下面。
