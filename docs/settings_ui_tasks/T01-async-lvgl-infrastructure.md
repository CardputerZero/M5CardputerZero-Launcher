# T01：公共异步 API/LVGL 生命周期

**固定 worker 目录**：`/home/nihao/w2T/github/launcher/projects/ui_test1`

## 目标

解决新 UI 同步组件 API 与真实异步后端之间的生命周期冲突，为 Wi‑Fi、Bluetooth、SoundCard、ADB 和更新任务提供统一约定。

## 负责范围

- `settings_tree_types.hpp`
- `settings_menu_roller.hpp`
- `lvgl_components.hpp`
- 新增 `settings_async_dispatch.hpp/.cpp` 或等价公共组件。

## 必须解决的问题

1. `LvSettingValuePage3Base::activate_selected()` 当前调用 API 后无条件 `LeaveSelfPage()`；异步提交时必须允许页面等待结果。
2. 开关组件需要明确 read、activate、pending、success、failure 状态。
3. 后端 callback 不能直接触碰 LVGL。
4. 页面析构后，队列中的旧结果必须丢弃。
5. `lv_async_call`、timer、任务注册器失败时不能泄漏对象。

## 建议接口

- lifetime token + generation。
- 后端 callback 只写入线程安全队列。
- LVGL timer 定期 drain 队列。
- 统一提供 `cancel()`、`join_all()`、`reap_finished()` 语义。
- 统一处理 pending 防抖和错误回滚。

## 验收

- 页面快速进入/退出 100 次无崩溃、UAF、线程泄漏。
- 请求完成前按 ESC 可安全退出或取消。
- 成功、失败、取消均恢复焦点。
- 重复 Enter 不会产生重复写操作。
- 无业务组件需要在 backend callback 中操作 LVGL。
