# 公共开发规范

所有 T00–T12B 负责人必须遵守本文件。组件任务书中的内容不能覆盖本文件的线程、API 和目录隔离要求。

## 1. 目标

保留新 Settings UI 的页面结构、视觉效果、键盘交互和异步能力，接入旧 Settings UI 已验证的业务接口以及 `cp0_lvgl` 后端 API。

不要复制旧 UI 的绘制代码；只复用旧实现中的 API 命令、参数、返回值解析、错误码、状态机和纯模型。

## 2. API 规则

- 优先使用 `cp0_lvgl` 已公开的强类型 C API。
- 需要字符串命令时使用已有 `cp0_signal_*` API。
- 不在 UI 层直接执行 `nmcli`、`amixer`、`bluetoothctl`、shell 或 sudo 命令。
- 需要权限的操作使用 `cp0_signal_system_admin_async`、`cp0_signal_sudo_argv_async` 或后端规定的异步接口。
- 每个命令必须符合后端 contract 的命令名、参数个数、范围、长度和控制字符限制。
- 同时检查 callback 的 `code` 和 `data`；解析失败必须提示并回滚 UI 状态。
- 不要为了迁就 UI 修改 `ext_components/cp0_lvgl` 后端；若确有后端 contract 缺陷，单独提交后端任务和测试。

## 3. 异步与 LVGL 线程规则

后端 callback 不得直接访问 LVGL。必须遵循：

1. 页面创建时建立 lifetime token、generation 或等价 owner 状态。
2. 后端 callback 只复制数据并入队。
3. 通过 `lv_timer`、`lv_async_call` 或 dispatch 队列在 LVGL 线程处理结果。
4. 处理结果前检查页面仍存活、generation 未变化、请求仍是当前请求。
5. 析构时停止扫描/取消请求、阻止新回调入队、清理 timer/event descriptor、等待任务结束，再删除 LVGL 对象。
6. 同类操作必须有 pending 防抖，禁止连续 Enter 产生重复写请求。
7. 成功、失败、取消三条路径都要恢复焦点和页面状态。

## 4. 修改边界

- 不修改别人的组件页面文件。
- 不直接修改中央 Settings 树；中央树只由 T00 合并。
- 新增 adapter、factory、model 和测试时使用独立文件。
- 旧 `legacy/settings` 不重新加入活动构建。
- `settings_page.cpp`、`settings_page.hpp`、`settings_page_api.cpp`、`builtin_app_registry.cpp` 默认只允许 T00 修改。

## 5. worker 创建与编译

### 固定目录分配

| 任务 | worker 目录 |
|---|---|
| T00 | `/home/nihao/w2T/github/launcher/projects/ui_test0` |
| T01 | `/home/nihao/w2T/github/launcher/projects/ui_test1` |
| T02 | `/home/nihao/w2T/github/launcher/projects/ui_test2` |
| T03 | `/home/nihao/w2T/github/launcher/projects/ui_test3` |
| T04 | `/home/nihao/w2T/github/launcher/projects/ui_test4` |
| T05 | `/home/nihao/w2T/github/launcher/projects/ui_test5` |
| T06 | `/home/nihao/w2T/github/launcher/projects/ui_test6` |
| T07 | `/home/nihao/w2T/github/launcher/projects/ui_test7` |
| T08 | `/home/nihao/w2T/github/launcher/projects/ui_test8` |
| T09 | `/home/nihao/w2T/github/launcher/projects/ui_test9` |
| T10 | `/home/nihao/w2T/github/launcher/projects/ui_test10` |
| T11 | `/home/nihao/w2T/github/launcher/projects/ui_test11` |
| T12A | `/home/nihao/w2T/github/launcher/projects/ui_test12` |
| T12B | `/home/nihao/w2T/github/launcher/projects/ui_test13` |

每名程序员只能在自己被分配的目录中修改。T00 的 `ui_test0` 是总工集成副本；最终合并仍由总工回到主仓库或指定集成分支执行。

```sh
cd /home/nihao/w2T/github/launcher/projects
cp -a ui_test ui_test0
cp -a ui_test ui_test1
cp -a ui_test ui_test2
# 继续创建 ui_test3 至 ui_test13
```

在自己的副本中确认路径和符号链接：

```sh
cd /home/nihao/w2T/github/launcher/projects/ui_test1
pwd
readlink -f main/ui
git status --short
```

SDL 本机构建：

```sh
export CONFIG_DEFAULT_FILE=linux_x86_sdl2_config_defaults.mk
scons -j8
```

CP0 交叉构建：

```sh
export CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk
scons -j8
```

后端 contract 测试：

```sh
cd /home/nihao/work/launcher/ext_components/cp0_lvgl
./tests/run_tests.sh
```

## 6. 交付资料

每名负责人必须提交：

- 修改文件清单。
- API 请求、返回 payload、错误码和 backend contract 文件。
- callback 线程以及回到 LVGL 线程的方式。
- 析构取消、token/generation 和任务 join 方案。
- SDL 构建结果；适用时提供 CP0 交叉构建结果。
- 单元测试、模型测试或手工验收步骤。
- 已知限制和未完成项。
