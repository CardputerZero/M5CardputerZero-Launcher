# 07 - 入力システムとキー割り当て

この章では、APPLaunch のキーボード入力スレッド、`key_item` イベント構造、LVGL イベント配送、ホーム画面と組み込みページのキー割り当て、ターミナル入力のエスケープ、デバッグ時の注意点を説明します。主なソースファイルは `ext_components/cp0_lvgl/include/keyboard_input.h`、`projects/APPLaunch/main/ui/ui.h`、`ext_components/cp0_lvgl/src/cp0/cp0_lvgl_keyboard.c`、`ext_components/cp0_lvgl/src/sdl/sdl_lvgl_keyboard.c`、`projects/APPLaunch/main/ui/ui_launch_page.cpp`、`projects/APPLaunch/main/ui/page_app/*.hpp` です。

## 1. 入力システムの概要

APPLaunch には 2 つの入力経路があります。

1. カスタム `LV_EVENT_KEYBOARD`: 完全な `struct key_item` を運び、多くのページが直接これを監視します。
2. LVGL indev key: `cp0_keypad_read_cb()` は evdev キーを `LV_KEY_*` にも変換し、LVGL の group/focus 機構で使えるようにします。

データフロー:

```text
Physical keyboard / SDL keyboard
        |
        v
libinput / SDL keyboard backend
        |
        v
keyboard_read_thread()
        |
        v
enqueue_key(struct key_item)
        |
        v
keyboard_queue + keyboard_mutex
        |
        v
cp0_keypad_read_cb()
        |
        +-- lv_obj_send_event(lv_screen_active(), LV_EVENT_KEYBOARD, key_item)
        +-- ui_global_hint_on_key(key_item)
        +-- data->key = cp0_evdev_process_key(key_code)
```

`LV_EVENT_KEYBOARD` は APPLaunch のカスタムイベントであり、LVGL 組み込みのキーイベントではありません。起動時に `main.cpp` で登録されます。

```cpp
if (LV_EVENT_KEYBOARD == 0)
    LV_EVENT_KEYBOARD = lv_event_register_id();
```

## 2. `key_item` データ構造

`ext_components/cp0_lvgl/include/keyboard_input.h` は入力イベントを定義します。

```c
struct key_item {
    uint32_t key_code;      // Linux evdev key code
    uint32_t keysym;        // primary XKB keysym
    uint32_t codepoint;     // Unicode code point, 0 if there is no character
    uint32_t mods;          // KBD_MOD_* modifier bitmap
    int      key_state;     // 0=released, 1=pressed, 2=repeat
    char     sym_name[65];  // XKB keysym name
    char     utf8[16];      // UTF-8 character
    char     flage;
    STAILQ_ENTRY(key_item) entries;
};
```

定数:

| Constant | 値/意味 |
| --- | --- |
| `KBD_KEY_RELEASED` | `0`、リリース |
| `KBD_KEY_PRESSED` | `1`、押下 |
| `KBD_KEY_REPEATED` | `2`、長押しリピート |
| `KBD_MOD_SHIFT` | Shift 修飾子 |
| `KBD_MOD_CTRL` | Ctrl 修飾子 |
| `KBD_MOD_ALT` | Alt 修飾子 |
| `KBD_MOD_LOGO` | Logo 修飾子 |
| `KBD_MOD_CAPS` | CapsLock 状態 |
| `KBD_MOD_NUM` | NumLock 状態 |

ページは物理キー判定に `key_code` を使うことも、テキスト入力の読み取りに `utf8` / `codepoint` を使うこともできます。

## 3. イベントマクロとページ側アクセスパターン

`projects/APPLaunch/main/ui/ui.h` は共通マクロを提供します。

```c
#define LV_EVENT_KEYBOARD_GET_KEY(e) \
    ((struct key_item *)lv_event_get_param(e))->key_code

#define LV_EVENT_KEYBOARD_GET_KEY_STATE(e) \
    ((struct key_item *)lv_event_get_param(e))->key_state

#define IS_KEY_PRESSED(e) \
    ((lv_event_get_code(e) == LV_EVENT_KEYBOARD) && \
     (LV_EVENT_KEYBOARD_GET_KEY_STATE(e) > 0))

#define IS_KEY_RELEASED(e) \
    ((lv_event_get_code(e) == LV_EVENT_KEYBOARD) && \
     (LV_EVENT_KEYBOARD_GET_KEY_STATE(e) == 0))
```

典型的なページイベントのバインド:

```cpp
void event_handler_init()
{
    lv_obj_add_event_cb(root_screen_, UIIpPanelPage::static_lvgl_handler,
                        LV_EVENT_ALL, this);
}

static void static_lvgl_handler(lv_event_t *e)
{
    auto *self = static_cast<UIIpPanelPage *>(lv_event_get_user_data(e));
    if (!self || !IS_KEY_RELEASED(e))
        return;

    uint32_t key = LV_EVENT_KEYBOARD_GET_KEY(e);
    self->handle_key(key);
}
```

注意: 多くのメニューページは press と repeat による重複トリガーを避けるため、キーリリース時だけ処理します。ゲーム系ページでは、移動や発射を press/repeat で処理することがあります。

## 4. デバイス側入力スレッド

デバイス実装は `ext_components/cp0_lvgl/src/cp0/cp0_lvgl_keyboard.c` にあります。

### 4.1 初期化

`init_input()` は 3 つの処理を行います。

```c
if (LV_EVENT_KEYBOARD == 0)
    LV_EVENT_KEYBOARD = lv_event_register_id();

pthread_create(&keyboard_read_thread_id, NULL,
               keyboard_read_thread, (void *)keyboard_device);

cp0_create_lvgl_input_devices();
```

キーボードデバイスのデフォルトは次の環境変数から取得されます。

```c
const char *keyboard_device = getenv("APPLAUNCH_LINUX_KEYBOARD_DEVICE");
```

環境変数が空の場合、`keyboard_read_thread()` は次のデフォルトを使います。

```text
/dev/input/by-path/platform-3f804000.i2c-event
```

このパスは `cp0_file_path("keyboard_device")` でも取得できます。

### 4.2 読み取りとキュー投入

`keyboard_read_thread()` は libinput でキーボードイベントを監視し、xkbcommon で `keysym`、`codepoint`、`utf8` を生成し、timerfd でリピートイベントを生成します。

キュー投入関数 `enqueue_key()`:

```c
static void enqueue_key(const struct key_item *src) {
    struct key_item *elm = malloc(sizeof(*elm));
    *elm = *src;

    if (elm->key_code == KEY_ESC)
        cp0_esc_state_write(elm->key_state);

    if (LVGL_RUN_FLAGE) {
        pthread_mutex_lock(&keyboard_mutex);
        STAILQ_INSERT_TAIL(&keyboard_queue, elm, entries);
        pthread_mutex_unlock(&keyboard_mutex);
    } else {
        free(elm);
    }
}
```

主なグローバル状態:

| Variable | 意味 |
| --- | --- |
| `keyboard_queue` | LVGL が消費するのを待っている `key_item` イベントのキュー |
| `keyboard_mutex` | キューのロック |
| `cp0_esc_state_*` | 長押し戻り / プロセス kill ロジックで使用する ESC 原子状態 C ABI |
| `LVGL_RUN_FLAGE` | LVGL が入力を受け付けるかどうか。外部アプリ実行中は 0 にされる場合がある |
| `LV_EVENT_KEYBOARD` | カスタム LVGL イベント id |

### 4.3 キュー取り出しと配送

`cp0_keypad_read_cb()` はキューからイベントを取り出し、現在のアクティブ画面へ配送します。

```c
lv_obj_t *root = lv_screen_active();
if (root)
    lv_obj_send_event(root, (lv_event_code_t)LV_EVENT_KEYBOARD, elm);

ui_global_hint_on_key(elm);

data->key = cp0_evdev_process_key(elm->key_code);
if (data->key) {
    data->state = (lv_indev_state_t)elm->key_state;
    data->continue_reading = !STAILQ_EMPTY(&keyboard_queue);
}
free(elm);
```

注意: `elm` はコールバック復帰後に解放されるため、ページは `lv_event_get_param(e)` で返されたポインタを長期保存してはいけません。非同期利用が必要ならフィールドをコピーしてください。

## 5. evdev から LVGL キーへの変換

`cp0_evdev_process_key()` は一部の Linux evdev キーを LVGL ナビゲーションキーへ変換します。

| evdev key | LVGL key |
| --- | --- |
| `KEY_UP` | `LV_KEY_UP` |
| `KEY_DOWN` | `LV_KEY_DOWN` |
| `KEY_LEFT` | `LV_KEY_LEFT` |
| `KEY_RIGHT` | `LV_KEY_RIGHT` |
| `KEY_ESC` | `LV_KEY_ESC` |
| `KEY_DELETE` | `LV_KEY_DEL` |
| `KEY_BACKSPACE` | `LV_KEY_BACKSPACE` |
| `KEY_ENTER` | `LV_KEY_ENTER` |
| `KEY_NEXT` | `LV_KEY_NEXT` |
| `KEY_PREVIOUS` | `LV_KEY_PREV` |
| `KEY_HOME` | `LV_KEY_HOME` |
| `KEY_END` | `LV_KEY_END` |

ページが `LV_EVENT_KEYBOARD` を直接処理する場合、通常は生の `KEY_*` 値を使います。ページが LVGL のウィジェットフォーカス機構へ委譲する場合は、`data->key` に依存します。

`ext_components/cp0_lvgl/include/input_keys.h` は Linux では `<linux/input.h>` をインクルードし、非 Linux プラットフォームでは一般的な互換 `KEY_*` 定義を提供するため、SDL/デスクトップビルドでもページコードをコンパイルできます。

## 6. ホーム画面のキー割り当て

ホーム画面のキー処理は `UILaunchPage::handle_home_key()` にあります。LVGL C コールバックの入口は `projects/APPLaunch/main/ui/ui_launch_page.cpp` の `UILaunchPage::on_home_key()` です。

まず、CardputerZero でよく使う `F/X/Z/C` キーを矢印キーへ対応付けます。

```cpp
static uint32_t fzxc_to_arrow(uint32_t key)
{
    switch (key) {
    case KEY_F: return KEY_UP;
    case KEY_X: return KEY_DOWN;
    case KEY_Z: return KEY_LEFT;
    case KEY_C: return KEY_RIGHT;
    default:    return key;
    }
}
```

ホーム画面の挙動:

| Input | Trigger timing | 挙動 |
| --- | --- | --- |
| `KEY_LEFT` or `Z` | pressed/repeat | `switch.wav` を再生し、`switch_right()` を呼び、右方向に次の項目へ回転 |
| `KEY_RIGHT` or `C` | pressed/repeat | `switch.wav` を再生し、`switch_left()` を呼び、左方向に次の項目へ回転 |
| `KEY_ENTER` | released | `enter.wav` を再生し、現在のアプリを起動 |
| `KEY_UP` / `KEY_DOWN` or `F` / `X` | pressed/repeat | ホーム画面では現在アクション未定義 |

注意: `handle_home_key()` は left/right キーを press 時に処理するため、長押しすると repeat イベントで連続切り替えが発生します。ENTER はキーを押し続けたまま起動が繰り返されないよう release 時に起動します。ログタグには古いデバッグ出力との互換性のため、まだ `main_key_switch` が含まれています。

## 7. 組み込みページのキー割り当て概要

各ページはそれぞれの `root_screen_` に `LV_EVENT_KEYBOARD` をバインドします。一般的な慣例は次のとおりです。

| Page | File | Main keys |
| --- | --- | --- |
| `UISTPage` | `ui_app_st.hpp` | ESC/arrow/Enter/Backspace は PTY 制御シーケンスへ変換。HOME 関連状態は終了/外部ロックに使用 |
| `UIGamePage` | `ui_app_game.hpp` | 矢印キーで移動、ENTER で開始/再開始、ESC で戻る |
| `UISetupPage` | `ui_app_setup.hpp` | UP/DOWN または F/X で選択、ENTER/RIGHT または C で入る/確定、ESC/LEFT または Z で戻る。一部ページは R/D 対応 |
| `UIGamePage` | `ui_app_game.hpp` | 共通ページキー処理を使用。ESC で戻る |
| `UIIpPanelPage` | `ui_app_ip_panel.hpp` | F/X/Z/C を LV_KEY_* へ変換。UP/DOWN で選択、ESC で戻る |
| `UISSHPage` | `ui_app_ssh.hpp` | UP/DOWN で Host/Port/User 切替、文字入力、BACKSPACE で削除、ENTER で接続、ESC で戻る |
| `UILoraPage` | `ui_app_lora.hpp` | KEY_UP/DOWN/LEFT/RIGHT/ENTER/ESC/BACKSPACE/DELETE を LV_KEY_* へ変換し、業務ロジックへ渡す |
| `UITankBattlePage` | `ui_app_tank_battle.hpp` | `33(F)` 上、`45(X)` 下、`44(Z)` 左、`46(C)` 右、`57(SPACE)` 発射、ESC で戻る |

## 8. F/X/Z/C 方向キーの慣例

CardputerZero キーボードでは、`F/X/Z/C` が矢印キーの代替としてよく使われます。コードベースには 3 つのパターンがあります。

1. ホーム画面 `ui_launch_page.cpp`: `fzxc_to_arrow()` が `F/X/Z/C` を `KEY_UP/DOWN/LEFT/RIGHT` へ変換します。
2. `UIGamePage` や `UIIpPanelPage` のように、ページ内で LVGL キーへ変換します。

```cpp
switch (key) {
case KEY_F: return LV_KEY_UP;
case KEY_X: return LV_KEY_DOWN;
case KEY_Z: return LV_KEY_LEFT;
case KEY_C: return LV_KEY_RIGHT;
}
```

3. ゲームでは evdev 番号を直接使うことがあります。`UITankBattlePage` では `KEY_MOVE_UP = 33`、`KEY_MOVE_DOWN = 45`、`KEY_MOVE_LEFT = 44`、`KEY_MOVE_RIGHT = 46` です。

新しいページでは `KEY_F` のようなシンボル名を優先し、生の数値は避けてください。過去のヒントとの互換性のために数値を残す場合は、対応するキー名をコメントで明記してください。

## 9. テキスト入力

SSH、Mesh、WiFi パスワード、ターミナルなど、一部ページでは文字入力が必要です。

### 9.1 単純な ASCII マッピング

`UISSHPage` と `UIMeshPage` は `keycode_to_char()` を使い、`KEY_1`、`KEY_Q` などを小文字へ変換します。

```cpp
static char keycode_to_char(uint32_t key)
{
    if (key >= KEY_1 && key <= KEY_9) return '1' + (key - KEY_1);
    if (key == KEY_0) return '0';
    if (key >= KEY_Q && key <= KEY_P) return qwerty[key - KEY_Q];
    if (key == KEY_SPACE) return ' ';
    if (key == 52) return '.';  // KEY_DOT
    if (key == 12) return '-';  // KEY_MINUS
    return 0;
}
```

この方法は単純ですが、Shift による大文字、入力メソッド、マルチバイト文字には対応しません。完全なテキスト入力能力が必要な場合は、`key_item::utf8` または `codepoint` を読んでください。

### 9.2 ターミナル入力

`UISTPage` は `struct key_item` を直接読み、物理キーと UTF-8 テキストを PTY バイトストリームへ変換します。

- `KEY_ENTER` -> `\r`
- `KEY_BACKSPACE` -> `0x7f`
- `KEY_ESC` -> `0x1b`
- 矢印キー -> application cursor mode に応じて `\033[A/B/C/D` または `\033OA/OB/OC/OD`
- 通常文字 -> `key_item::utf8`

ターミナルページは子プロセス終了、画面更新、カーソル点滅、ESC/Home の戻りセマンティクスも扱うため、通常のページより複雑です。

## 10. 外部アプリ実行中の入力処理

外部アプリは `Launch::launch_Exec()` から起動されます。

```cpp
LVGL_RUN_FLAGE = 0;
lv_indev_set_group(indev, NULL);
lv_timer_enable(false);

int ret = cp0_process_exec_blocking(exec.c_str(), keep_root ? 1 : 0);

lv_timer_enable(true);
launch_page_->show_home_screen();
LVGL_RUN_FLAGE = 1;
```

意味:

- 外部プロセス実行中、APPLaunch は LVGL タイマーを停止し、通常のキュー経由キーボードイベントを受け取らなくなります。
- ESC 状態は `cp0_esc_state_write()` で更新され、外部プロセス復帰ロジックは `cp0_esc_state_read()` で読み取ります。
- 外部プロセス終了後、ホーム画面、入力グループ、LVGL タイマーが復元されます。

## 11. 入力グループの切り替え

ホーム画面とページはそれぞれ独自の LVGL group を持っています。

- ホーム画面: `UILaunchPage::home_input_group()`。
- 組み込みページ: `AppPageRoot::input_group()`。
- ネストしたターミナル: `UISTPage::input_group()`。

ページを切り替えるときは、同時に入力グループも切り替える必要があります。

```cpp
lv_disp_load_scr(p->screen());
lv_indev_set_group(lv_indev_get_next(NULL), p->input_group());
```

ホーム画面へ戻る場合:

```cpp
launch_page_->show_home_screen();
```

`show_home_screen()` はホーム画面を読み込み、`UILaunchPage::bind_home_input_group()` を呼びます。

画面は切り替わったのに group が古いページを指していると、次のような問題が起こります。

- 表示中のページがキーに反応しない。
- 非表示のページがキーに反応する。
- ネストしたページから出た後の ESC 挙動が異常になる。

## 12. 入力問題のデバッグ

デバイスキーボード層には既にログがあります。

```text
[KBD] enqueue code=... state=... sym=... utf8=... cp=... mods=... run=... home_flag=...
[INDEV] dequeue code=... state=... sym=... utf8=... cp=... active_screen=...
[LAUNCHER] main_key_switch raw=...->code=... state=... sym=...
```

推奨する調査順序:

1. `keyboard_read_thread()` が起動したか、デバイスパスが正しいかを確認します。
2. `[KBD] enqueue` が出るか確認します。出なければ libinput/device/xkb 層の問題です。
3. `[INDEV] dequeue` が出るか確認します。出なければキューが LVGL indev に消費されていない可能性があります。
4. `active_screen` が現在のページ画面か確認します。
5. ページが `root_screen_` に `LV_EVENT_KEYBOARD` をバインドしているか確認します。
6. ページが press、release、repeat のどれを処理しているか、トリガータイミングが不整合でないか確認します。
7. `LVGL_RUN_FLAGE` が 0 か確認します。外部アプリ実行中は通常イベントが破棄されます。

## 13. 新規ページのキー処理に関する推奨事項

新しいページでは次のルールに従ってください。

- リスト/メニューページ: `IS_KEY_RELEASED(e)` で `KEY_UP/DOWN/LEFT/RIGHT/ENTER/ESC` を処理します。
- ゲームページ: 連続動作は `IS_KEY_PRESSED(e)` で処理し、必要なら repeat も受け付けます。
- テキスト入力ページ: `key_item::utf8` を優先します。`keycode_to_char()` は単純な場合だけ使います。
- 戻るキー: ESC は現在のページまたは現在のポップアップを閉じる必要があります。多階層ビューではまず前の階層に戻り、その後ホームへ戻ります。
- 方向代替キー: デバイスキーボード対応ページでは `F/X/Z/C` を一貫してサポートしてください。
- `struct key_item *` ポインタを保存しないでください。非同期処理が必要なら `key_code`、`utf8` などのフィールドをコピーします。
- 長押し可能なキーでは、確認や起動が繰り返されないよう `KBD_KEY_PRESSED`、`KBD_KEY_REPEATED`、`KBD_KEY_RELEASED` を明示的に区別してください。
