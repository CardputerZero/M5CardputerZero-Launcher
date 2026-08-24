# T00 总工：架构、中央树与最终合并

**固定 worker 目录**：`/home/nihao/w2T/github/launcher/projects/ui_test0`

## 目标

维护集成基线，负责中央 Settings 树、APPLaunch 入口、组件 factory 绑定和最终验收；不在中央文件中实现所有业务逻辑。

## 负责文件

- `/home/nihao/work/launcher/projects/APPLaunch/main/ui/builtin_app_registry.cpp`
- `/home/nihao/work/launcher/projects/ui_test/main/ui/settings_page.cpp`
- `/home/nihao/work/launcher/projects/ui_test/main/ui/settings_page.hpp`
- `/home/nihao/work/launcher/projects/ui_test/main/ui/settings_page_api.cpp`
- 必要时新增 `settings_integration_*.{hpp,cpp}`。

## 工作内容

1. 建立 Settings 树最终节点表和组件 factory 注册表。
2. 接收 T03–T12B 的 adapter/factory，统一绑定到中央树。
3. 删除 `mork_api` 占位逻辑。
4. 清理 Ethernet、Account、Update、About、Help、Info 中的空节点。
5. 确认 `LAUNCHER_BUILD` 与独立 `ui_test` 两种编译条件都成立。
6. 处理动态 Launcher 子树的创建、销毁和 registry 状态刷新。
7. 按阶段合并，记录冲突、回归结果和已知问题。

## 合并顺序

1. T01 公共异步基础设施。
2. T02 构建/worker 隔离。
3. T05、T07、T08、T09、T10 基础读写页面。
4. T03、T04、T06、T11 复杂异步页面。
5. T12A、T12B 系统与 Launcher 补齐。
6. 最终 APPLaunch 集成和全量回归。

## 合并拒绝条件

- 保留 Mock 或 `mork_api`。
- 后端线程直接调用 `lv_*`。
- 直接 shell 绕过 `cp0_lvgl`。
- 异步请求缺少取消/析构处理。
- 失败后 UI 不回滚。
- 只通过 `projects/ui_test`，未通过 APPLaunch `LAUNCHER_BUILD`。
- 修改越过任务边界。

## 验收

- 所有 Settings 节点均有明确业务或明确标记为静态信息。
- APPLaunch 可进入 Settings、返回主页并重复进入退出。
- 全部组件通过功能、异步生命周期和错误路径回归。
