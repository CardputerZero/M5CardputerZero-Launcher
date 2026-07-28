# 04 - Application Model and Launch Mechanism

この章では、APPLaunch が組み込みページ、端末コマンド、外部スタンドアロンプログラムを 1 つのアプリケーションリストへ統合する方法と、ユーザーが Enter を押した後にアプリケーションがどのように起動されるかを説明します。主な参照先は `projects/APPLaunch/main/ui/launch.cpp`、`projects/APPLaunch/main/ui/launch.h`、`projects/APPLaunch/main/ui/ui_launch_page.cpp`、`projects/APPLaunch/main/ui/page_app/*` です。

## 1. Application Model Overview

APPLaunch はホーム画面上の各エントリを `app` として抽象化します。

```text
app
├── Name  display title
├── Icon  icon path
├── Exec  external command; can be empty for built-in pages
└── launch(Launch*)  launch action
```

この統一により、ホームカルーセルはアプリケーション種別を意識する必要がありません。`Name` と `Icon` を表示し、Enter が押されたら現在の `app.launch()` を呼ぶだけです。

```text
Home center card
  -> Launch::launch_app()
  -> Launch::launch_app()
  -> app.launch(this)
      ├── Built-in page: new PageT + lv_disp_load_scr()
      ├── Terminal app: UISTPage + PTY exec()
      └── External app: cp0_process_exec_blocking()
```

## 2. Key Source Paths

| Path | Description |
| --- | --- |
| `projects/APPLaunch/main/ui/launch.h` | 公開 `Launch` インターフェースと app モデル宣言 |
| `projects/APPLaunch/main/ui/launch.cpp` | `app`、`Launch`、アプリケーションリスト、起動ロジック、`.desktop` スキャン |
| `projects/APPLaunch/main/ui/ui_launch_page.cpp` | Enter / クリックイベントを `Launch::launch_app()` へ転送する |
| `projects/APPLaunch/main/ui/page_app/ui_app_st.hpp` | 端末ページ `UISTPage` |
| `projects/APPLaunch/main/ui/page_app/*.hpp` | settings、game、file、camera、LoRa などの組み込みページ |
| `projects/APPLaunch/APPLaunch/applications/` | 実行時 `.desktop` アプリケーション記述ディレクトリ |
| `ext_components/cp0_lvgl` | プロセス起動、PTY、ディレクトリ監視、パス解決などの低レベル機能 |

## 3. `Launch` Runtime State

`launch.h` は `Launch` クラスを直接公開しています。現在のコードには独立した `LaunchImpl` レイヤーはなく、アプリケーションリスト、ディレクトリ watcher、現在ページの保持、カルーセルヘルパーはすべて `Launch` にあります。

重要な private 状態:

| Field | Description |
| --- | --- |
| `launch_page_` | ホーム `UILaunchPage` への weak reference |
| `current_app` | 現在中央カードに対応するアプリケーション index。デフォルトは `2` なので、初期中央カードは CLI |
| `dir_watcher_` / `watch_timer_` | `applications/` ディレクトリを監視し、動的アプリを再読み込みする |
| `fixed_count` | 組み込み/固定アプリケーション数。動的再読み込みではこの位置より前の要素を保持する |
| `app_list` | 組み込みエントリと動的 `.desktop` エントリ |
| `app_Page` | 現在の組み込みページまたは端末ページの lifetime holder |

`Launch::bind_ui()` は初期リストを構築し、動的 `.desktop` ファイルを読み込み、ディレクトリ watcher タイマーを開始し、app-registry 変更コールバックを登録します。

## 4. `app` Structure and Three Launch Modes

`app` は `launch.cpp` で定義されています。

```cpp
struct app
{
    std::string Name;
    std::string Icon;
    std::string Exec;

    std::function<void(Launch *)> launch;

    app(std::string name, std::string icon, std::string exec, bool terminal);
    app(std::string name, std::string icon, std::string exec, bool terminal, bool sysplause);
    app(std::string name, std::string icon, std::string exec, bool terminal, bool sysplause, bool run_as_root);

    template <class PageT>
    app(std::string name, std::string icon, page_t<PageT> tag);
};
```

3 種類のアプリケーションカテゴリ:

| Type | Construction | Launch function | Examples |
| --- | --- | --- | --- |
| Built-in page | `page_v<PageT>` / `append_page_app<PageT>` | ページを構築し `cp0_lvgl_start_app_page()` を呼ぶ | `CLI`, `GAME`, `SETTING`, `LORA` |
| Terminal command | `exec, terminal=true` | `launch_Exec_in_terminal()` が `UISTPage` を作成して `exec()` を呼ぶ | `Python`, `Terminal=true` の `.desktop` アプリ |
| External process | `exec, terminal=false` | `launch_Exec()` | AppStore, Calculator |

## 5. Fixed Application Registration

固定エントリは `builtin_app_registry.cpp` の `BUILTIN_APPS[]` として宣言されています。各エントリは descriptor、外部 command メタデータ、または組み込みページ appender を持ちます。

代表的なエントリ:

```cpp
constexpr BuiltinAppRegistration BUILTIN_APPS[] = {
    {{"Python", "python_100.png", "app_Python", false, true}, "python3", true, false, false, nullptr},
    {{"STORE", "store_100.png", "app_Store", false, true},
     "/usr/share/APPLaunch/bin/M5CardputerZero-AppStore", false, true, true, nullptr},
    {{"CLI", "cli_100.png", "app_CLI", false, true}, nullptr, false, true, false, append_page_app<UISTPage>},
    {{"GAME", "game_100.png", "app_Game", false, true}, nullptr, false, true, false, append_page_app<UIGamePage>},
    {{"SETTING", "setting_100.png", "app_Setting", false, true}, nullptr, false, true, false, append_page_app<UISetupPage>},
    {{"MATH", "math_100.png", "app_Math", true, false},
     "/usr/share/APPLaunch/bin/M5CardputerZero-Calculator", false, true, false, nullptr},
};
```

`Launch::rebuild_builtin_apps()` はリストをクリアし、`launcher_app_registry_is_enabled()` を呼びながら有効な組み込みアプリを追加し、`fixed_count` を更新します。Settings の変更は `launcher_app_registry_set_enabled()` で保存され、その後 `Launch::applications_reload()` を発火します。

最初の 5 エントリが 5 スロットのホームカルーセルを初期化します。

```text
slot 0 far-left : Python
slot 1 left     : STORE
slot 2 center   : CLI
slot 3 right    : GAME
slot 4 far-right: SETTING
current_app     : 2
```

## 6. Built-in Page Launch Mechanism

組み込みページは template constructor を通して構築されます。

```cpp
template <class PageT>
app::app(std::string name, std::string icon, page_t<PageT>)
    : Name(std::move(name)), Icon(std::move(icon))
{
    launch = [](Launch *self)
    {
        ui_loading_show("Loading...");
        lv_refr_now(NULL);

        auto p = std::make_shared<PageT>();
        self->app_Page = p;
        lv_disp_load_scr(p->screen());
        lv_indev_set_group(lv_indev_get_next(NULL), p->input_group());
        p->navigate_home = std::bind(&Launch::go_back_home, self);

        ui_loading_hide();
    };
}
```

組み込みページは次の規約に従う必要があります。

- ページクラスは引数なしで構築できること。
- ページのルート画面を返す `screen()` を提供すること。
- ページ自身の入力グループを返す `input_group()` を提供すること。
- ホームに戻るための `navigate_home` コールバックを提供または継承すること。

起動シーケンス:

```text
Enter
  -> app.launch(Launch*)
  -> ui_loading_show("Loading...")
  -> lv_refr_now(NULL)
  -> make_shared<PageT>()
  -> app_Page = p keeps the lifetime
  -> lv_disp_load_scr(p->screen())
  -> Input device switches to p->input_group()
  -> p->navigate_home = Launch::go_back_home
  -> ui_loading_hide()
```

## 7. Terminal Application Launch Mechanism

端末アプリケーションは `UISTPage` を使用し、外部コマンドは APPLaunch プロセス内の端末ページで実行されます。

```cpp
void launch_Exec_in_terminal(const std::string &exec, bool sysplause = true)
{
    ui_loading_show("Loading...");
    lv_refr_now(NULL);

    auto p = std::make_shared<UISTPage>();
    app_Page = p;
    lv_disp_load_scr(p->screen());
    lv_indev_set_group(lv_indev_get_next(NULL), p->input_group());
    p->navigate_home = std::bind(&Launch::go_back_home, this);
    p->terminal_sysplause = sysplause;

    ui_loading_hide();
    p->exec(exec);
}
```

典型的なエントリ:

```text
Python -> exec = "python3", terminal = true
CLI    -> 組み込み UISTPage。ページ内部で対話型 bash を起動
```

組み込みページと比べ、端末アプリケーションには `p->exec(exec)` という追加ステップがあります。通常、コマンドとは PTY を通してやり取りします。ユーザーが見るのは、APPLaunch の外にある別 UI ではなく `UISTPage` です。

## 8. External Standalone Application Launch Mechanism

外部アプリケーションは `cp0_process_exec_blocking()` を使用します。

```cpp
void launch_Exec(const std::string &exec, bool keep_root = false)
{
    ui_loading_show("Loading...");

    lv_disp_t *disp = lv_disp_get_default();
    lv_indev_t *indev = lv_indev_get_next(NULL);

    LVGL_RUN_FLAGE = 0;
    if (indev)
        lv_indev_set_group(indev, NULL);
    lv_timer_enable(false);
    lv_refr_now(disp);

    int ret = cp0_process_exec_blocking(exec.c_str(), keep_root ? 1 : 0);

    lv_timer_enable(true);
    if (indev)
        lv_indev_set_group(indev, UILaunchPage::home_input_group());
    if (launch_page_)
        launch_page_->show_home_screen();
    ui_loading::hide();
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);
    LVGL_RUN_FLAGE = 1;
}
```

重要な点:

- 起動前に Loading を表示して強制更新するため、ユーザーは即座にフィードバックを得られます。
- APPLaunch の入力グループを解除し、外部プロセス実行中にホーム画面がキー処理を続けないようにします。
- `lv_timer_enable(false)` は、外部プログラムがフォアグラウンドを取っている間 LVGL タイマーを一時停止します。
- `cp0_process_exec_blocking()` は外部プログラムが終了するまでブロックします。
- 外部プログラム終了後、タイマーを復元し、`launch_page_->show_home_screen()` を呼び、`LVGL_RUN_FLAGE` を戻します。

シーケンス:

```text
Enter external app
  -> ui_loading_show()
  -> LVGL_RUN_FLAGE=0
  -> lv_indev_set_group(NULL)
  -> lv_timer_enable(false)
  -> lv_refr_now()
  -> cp0_process_exec_blocking()
      -> External program runs
      -> APPLaunch main rendering is paused
      -> Wait for the external program to exit
  -> lv_timer_enable(true)
  -> launch_page_->show_home_screen()
  -> ui_loading_hide()
  -> lv_refr_now()
  -> LVGL_RUN_FLAGE=1
```

`STORE` は外部アプリケーションを起動する固定エントリです。

```cpp
{{"STORE", "store_100.png", "app_Store", false, true},
 "/usr/share/APPLaunch/bin/M5CardputerZero-AppStore", false, true, true, nullptr},
```

ここでは `run_as_root=true` が `launch_Exec(exec, run_as_root)` に渡され、そこで `keep_root ? 1 : 0` に変換されます。

## 9. Return-to-Home Mechanism

組み込みページと端末ページは、`navigate_home` コールバックを通してホーム画面へ戻ります。

```cpp
void go_back_home()
{
    lv_async_call(lv_go_back_home, this);
}

static void lv_go_back_home(void *arg)
{
    auto self = (Launch *)arg;
    lv_timer_enable(true);
    if (self->launch_page_)
        self->launch_page_->show_home_screen();
    lv_refr_now(NULL);
    if (self->app_Page)
        self->app_Page.reset();
}
```

`lv_async_call()` を使う理由:

- ホーム復帰はページイベントや入力コールバックから発火する場合があります。
- 非同期で実行することで、現在の LVGL イベントスタック内でページオブジェクトを直接破棄することを避けます。
- `app_Page.reset()` は現在ページを解放するため、そのページオブジェクトが以後使われないことを保証する必要があります。

外部アプリケーションは `navigate_home` を使いません。代わりに `cp0_process_exec_blocking()` が戻った後でホーム画面を復元します。

## 10. `.desktop` Dynamic Application Scanning

動的アプリケーションディレクトリ:

```cpp
const std::string app_dir_path = cp0_file_path("applications");
```

デバイスにインストールされた後、通常は次に対応します。

```text
/usr/share/APPLaunch/applications/
```

`.desktop` ファイル例:

```ini
[Desktop Entry]
Name=Vim
TryExec=vim
Exec=vim
Terminal=true
Icon=share/images/e-Mail_80.png
```

`applications_load()` は `.desktop` 拡張子のファイルだけを処理し、`[Desktop Entry]` セクションからフィールドを読み取ります。

| Field | Required | Description |
| --- | --- | --- |
| `Name` | Yes | ホーム画面に表示するタイトル |
| `Exec` | Yes | 起動コマンド |
| `Icon` | No | アイコンパス |
| `Terminal` | No | `true/True/1` は `UISTPage` 経由で起動することを意味する |
| `Sysplause` | No | 端末ページへ渡す pause policy。デフォルトは true |

登録ロジック:

```cpp
if (page_title.empty() || app_exec.empty())
    continue;

for (auto it : app_list) {
    if (it.Exec == app_exec) {
        in_list = true;
        break;
    }
}

if (!in_list)
    app_list.emplace_back(page_title, app_icon, app_exec,
                          app_terminal, app_sysplause);
```

注意: 動的アプリケーションは `Exec` によって重複排除されます。`Exec` が固定アプリまたは別の `.desktop` アプリと一致する場合、そのエントリはスキップされます。

## 11. Dynamic Application Directory Watching and Reloading

`Launch` コンストラクタの末尾:

```cpp
fixed_count = app_list.size();
applications_load();
inotify_init_watch();
watch_timer = lv_timer_create(app_dir_watch_cb, 3000, this);
```

監視フロー:

```text
Every 3 seconds via LVGL timer
  -> cp0_dir_watch_poll(dir_watcher)
  -> If applications/ changed
      -> applications_reload()
          -> Delete dynamic apps after fixed_count
          -> applications_load()
          -> refresh_ui_panels()
```

`refresh_ui_panels()` は現在の `current_app` に基づき、表示/非表示の 5 スロットを書き換えます。

```cpp
app_at(current_app - 2) -> far-left
app_at(current_app - 1) -> left
app_at(current_app)     -> center
app_at(current_app + 1) -> right
app_at(current_app + 2) -> far-right
```

これにより、動的アプリケーションの追加や削除後でも、ホーム画面は LVGL オブジェクトを再構築する必要がなく、テキストとアイコンだけを更新します。

## 12. Icon Setting and Resource Paths

アイコンは `panel_set_icon()` によって設定されます。

```cpp
static void panel_set_icon(lv_obj_t *panel, const char *src)
{
    lv_obj_t *img = lv_obj_get_child(panel, 0);
    if (!img || !lv_obj_check_type(img, &lv_image_class)) {
        img = lv_image_create(panel);
        lv_obj_set_size(img, LV_PCT(100), LV_PCT(100));
        lv_obj_set_align(img, LV_ALIGN_CENTER);
        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_STRETCH);
    }
    lv_image_set_src(img, icon_src);
}
```

特徴:

- 各 panel は最初の child image を再利用し、画像オブジェクトを繰り返し作成しません。
- 画像は panel サイズに stretch されます。
- パスが空または読み取れない場合、ログを出しますが `lv_image_set_src()` は呼びます。

固定アプリケーションは一般に `img_path("xxx.png")` を使います。動的 `.desktop` アプリケーションの `Icon` フィールドは、現在 `app_icon` としてそのまま渡されます。`.desktop` ファイルを書くときは、LVGL がアイコンパスを読めることを確認してください。

## 13. Complete Flow from Key Press to Launch

```text
User releases ENTER
  -> LV_EVENT_KEYBOARD is delivered to UILaunchPage::screen()
  -> UILaunchPage::on_home_key()
      -> handle_home_key()
      -> code == KEY_ENTER and key_state == 0
      -> audio_play_enter()
  -> UILaunchPage::launch_selected_app()
      -> launch_->launch_app()
  -> Launch::launch_app()
      -> impl_->launch_app()
  -> Launch::launch_app()
      -> auto it = std::next(app_list.begin(), current_app)
      -> it->launch(this)
  -> Enter built-in page / terminal page / external process based on app type
```

## 14. Notes

- `Launch::bind_ui()` は `Launch` 作成前に呼ばれる必要があります。そうしないとホーム画面は表示されても、アプリケーションリスト更新、ステータスバータイマー、ディレクトリ監視、起動ロジックが動作しません。
- `current_app` のデフォルトは `2` です。最初の 5 つの固定エントリ順は初期中央カードに影響します。この順序を変更するときは、初期ホーム体験を考慮してください。
- 組み込みページの構築に時間がかかる可能性がある場合は、ユーザーが即時フィードバックを得られるよう `ui_loading_show()` + `lv_refr_now()` を維持してください。
- 外部アプリケーションの起動は APPLaunch の LVGL タイマーと入力グループを一時停止します。外部プログラムは正常終了するか HOME ロジックに応答する必要があります。そうしないと、ユーザーは外部 UI に閉じ込められたように感じます。
- 動的 `.desktop` アプリケーションには少なくとも `Name` と `Exec` が必要です。`Terminal=true` はコマンドラインプログラムに適しており、グラフィカルまたは排他的 framebuffer プログラムでは `Terminal=false` を使うべきです。
- 動的アプリケーションは `Name` ではなく `Exec` で重複排除されます。同じコマンドを使う複数エントリがある場合、最初の 1 つだけが保持されます。
- `applications/` を変更した後は、watcher が再読み込みするまで最大 3 秒待ってください。watcher が初期化されていない、またはプラットフォームが対応していない場合は、APPLaunch を再起動して変更を確認してください。
