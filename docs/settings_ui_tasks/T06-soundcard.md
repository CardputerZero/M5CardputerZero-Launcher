# T06：SoundCard/ALSA Mixer

**固定 worker 目录**：`/home/nihao/w2T/github/launcher/projects/ui_test6`

## 目标

将新声卡页面从 Mock 控件改为真实 `cp0_signal_soundcard_api`，复用旧 SoundCardModel 的解析和范围规则。

## 负责文件

- `settings_sound_card_page.hpp`
- `settings_sound_card_detail_page.hpp`
- 新增声卡 adapter、解析测试和模型测试。

## API

- `ListCards`
- `ListControls card_index`
- `GetControlDetail card_index control`
- `SetControl card_index control value`

## 必须删除

- `mock_cards()`
- `mock_controls()`
- `Applied (mock)`
- 其他仅用于演示的假声卡数据。

## 工作内容

1. 使用 `cp0_alsa_parser` 规则解析声卡和控件 payload。
2. 复用旧 `SoundCardModel` 的 clamp、控件类型和当前值解析。
3. 处理无声卡、无控件、不可写控件和写入失败。
4. 切换 card/control 时取消或失效化前一请求。
5. 写入成功后重新读取 detail，显示 backend 实际值。

## 约束

- UI 不能直接执行 `amixer`。
- backend callback 不得触碰 LVGL。
- 页面析构必须停止请求并等待任务结束。

## 验收

- 真实声卡列表、控件列表和 detail 能显示。
- 数值超范围时 clamp 或提示行为与旧模型一致。
- 写入成功、失败、后端不可用均有反馈。
- 快速切换 card/control 不会旧结果覆盖新页面。
