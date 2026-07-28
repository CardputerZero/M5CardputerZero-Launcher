# 02 - Runtime Framework and Boot Flow

この章では、APPLaunch プロセスのエントリポイントからホーム画面の最初のフレームが表示されるまでの全体経路を説明します。主な参照先は `projects/APPLaunch/main/src/main.cpp`、`projects/APPLaunch/main/ui/ui.cpp`、`projects/APPLaunch/main/ui/launcher_ui_runtime.cpp`、`projects/APPLaunch/main/ui/ui_launch_page.cpp` です。

## 1. Runtime Framework Overview

APPLaunch は単一プロセスの LVGL アプリケーションです。プロセス入口は APPLaunch 固有のループを持たず、`cp0_lvgl` の共有 runner `cp0_lvgl_run()` に setup/teardown callback を渡します。

```text
APPLaunch process
├── main.cpp
│   └── cp0_lvgl_run(options)
│       ├── lv_init() / cp0_lvgl_init()
│       ├── setup callback: launcher_ui::init() / ui_screensaver_init()
│       ├── lv_timer_handler() + semaphore wait
│       └── teardown callback: launcher_ui::deinit()
└── launcher_ui::init()
    └── LauncherUiRuntime()
        ├── create_display()
        ├── Create Launch / UILaunchPage bound objects
        └── build_launcher_home()
```

主要な特徴:

- LVGL 初期化とプラットフォーム適応初期化は共有 runner で一度だけ実行されます。
- ホーム UI は `LauncherUiRuntime` の制御下で作成され、実際のオブジェクトは `UILaunchPage::create_screen()` で作られます。
- `Launch` / `Launch` は、アプリケーションリスト、起動方式、ステータスバー更新、動的アプリケーションディレクトリの監視を担当します。
- `ui_init()` の直後に `lv_obj_invalidate()` + `lv_refr_now(NULL)` で最初のホームフレームを強制更新し、起動後の自然な更新を待つ間に黒画面が出ることを避けます。

## 2. Entry Files and Key Source Paths

| Path | Role |
| --- | --- |
| `projects/APPLaunch/main/src/main.cpp` | runner options と APPLaunch の setup/teardown callback |
| `ext_components/cp0_lvgl/src/cp0_lvgl_app_runner.cpp` | LVGL/プラットフォーム初期化、timer loop、semaphore wait、service shutdown |
| `projects/APPLaunch/main/ui/ui.cpp` | `ui_init()`、グローバルな `LauncherUiRuntime home` を作成する |
| `projects/APPLaunch/main/ui/launcher_ui_runtime.cpp` | LVGL テーマを設定し、ホーム画面を作成し、Launch 連携オブジェクトを作成する |
| `projects/APPLaunch/main/ui/ui_launch_page.cpp` | ホーム画面、起動 GIF、ホーム loading、入力グループ |
| `projects/APPLaunch/main/ui/launch.cpp` | アプリケーションマネージャ。外部/端末/組み込みページを起動し、ステータスバータイマーを所有する |
| `ext_components/cp0_lvgl` | `cp0_lvgl_init()`、ファイルパス、入力、プロセス、システム機能のラッパー |

## 3. `main()` Boot Flow

`main()` のフレームワークコードは次のとおりです。

```cpp
int main(void)
{
    Cp0LvglRunOptions options;
    options.setup = []() {
        if (LV_EVENT_KEYBOARD == 0) LV_EVENT_KEYBOARD = lv_event_register_id();
        launcher_ui::init();
        ui_screensaver_init();
        return true;
    };
    options.teardown = []() { launcher_ui::deinit(); };
    return cp0_lvgl_run(std::move(options));
}
```

### 3.1 Initialization Phase

1. runner が `lv_init()` と `cp0_lvgl_init()` を実行します。
2. default display が存在することを確認した後、APPLaunch の setup callback を呼びます。
3. setup が `LV_EVENT_KEYBOARD` を登録し、`launcher_ui::init()` と `ui_screensaver_init()` を実行します。
4. runner が最初の invalidate/refresh を行い、`lv_timer_handler()` を駆動します。timer がない場合は semaphore、ある場合は期限付き wait を使います。
5. 終了時は teardown 後に sudo/RPC/camera/audio/PTY/input/WiFi/LoRa/battery/LVGL を順序どおり停止します。

### 3.2 First-Frame Refresh

`ui_init()` が戻った後、コードはすぐに次を実行します。

```cpp
lv_obj_invalidate(lv_scr_act());
lv_refr_now(NULL);
```

このステップの目的は通常の更新ではなく、現在のアクティブ画面の内容を framebuffer/SDL window へ強制的に flush することです。ホームオブジェクトが作成された直後に、後続の `lv_timer_handler()` だけに頼ると一瞬黒画面が見える場合があります。最初のフレームを強制することで、起動時の挙動をより決定的にします。

### 3.3 Shared Runner Loop

ループは固定 5 ms polling ではなく、`lv_timer_handler()` の次回期限を使います。

```text
Each loop iteration
  -> lv_timer_handler()
  -> no timer: wait on semaphore
  -> timer ready: timed wait until next deadline
```

- `lv_timer_handler()` は LVGL タイマー、アニメーション、入力イベント、再描画を駆動します。
- `cp0_lvgl_wake()` は resume callback から semaphore を post し、新しい timer や作業をすぐに処理します。

## 4. From `ui_init()` to Home Object Creation

`ui_init()` は `projects/APPLaunch/main/ui/ui.cpp` にあります。

```cpp
std::unique_ptr<LauncherUiRuntime> home;

void ui_init(void)
{
    home = std::make_unique<LauncherUiRuntime>();
}
```

`LauncherUiRuntime` コンストラクタは次の処理へ進みます。

```cpp
LauncherUiRuntime::LauncherUiRuntime()
{
    create_display();

    launch_ = std::make_shared<Launch>();
    launch_page_ = std::make_shared<UILaunchPage>(launch_);
    launch_->set_launch_page(launch_page_);

    build_launcher_home();
}
```

ここでは順序に注意してください。

1. `create_display()` が最初にフォントマネージャを作成し、LVGL テーマを設定します。
2. `Launch` と `UILaunchPage` を構築し、`Launch::set_launch_page()` によって双方向の協調関係を確立します。
3. `build_launcher_home()` がホーム画面を作成し、`Launch::bind_ui()` を呼んでアプリケーションリストを構築し、入力グループを初期化し、ホーム画面または起動 GIF を表示します。

## 5. Display / Theme Initialization

`LauncherUiRuntime::create_display()` の中核コード:

```cpp
void LauncherUiRuntime::create_display()
{
    fonts_ = std::make_shared<LauncherFonts>();

    dispp_ = lv_disp_get_default();
    theme_ = lv_theme_default_init(
        dispp_,
        lv_palette_main(LV_PALETTE_BLUE),
        lv_palette_main(LV_PALETTE_RED),
        false,
        LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp_, theme_);
}
```

注意点:

- `LauncherFonts` は、ホーム画面とページで共有される FreeType フォントキャッシュです。入口関数は `launcher_fonts()` です。
- `lv_disp_get_default()` は、`cp0_lvgl_init()` がすでに表示デバイスを登録していることに依存します。
- このテーマはベーステーマにすぎません。多くのホームコントロールは、サイズ、色、背景画像、フォントを `ui_launch_page.cpp` 内で手動設定します。

## 6. Home Creation and Display Flow

`LauncherUiRuntime::build_launcher_home()` はホーム画面を表示するための主要なエントリポイントです。

```cpp
void LauncherUiRuntime::build_launcher_home()
{
    LV_EVENT_GET_COMP_CHILD = lv_event_register_id();

    launch_page_->create_screen();
    launch_->bind_ui();
    launch_page_->init_input_group();

#ifndef APPLAUNCH_STARTUP_ANIMATION
    launch_page_->load_home_screen();
#else
#ifdef HAL_PLATFORM_SDL
    launch_page_->load_home_screen();
#else
    const char *gif_path = cp0_file_path_c("logo_output.gif");
    FILE *gif_file = fopen(gif_path, "r");
    if (gif_file) {
        fclose(gif_file);
        launch_page_->start_startup_gif();
    } else {
        launch_page_->load_home_screen();
    }
#endif
#endif
}
```

### 6.1 Home Screen Creation

`UILaunchPage` は `home_base` を継承しているため、ルート画面、上部ステータスバー、コンテンツコンテナ、入力グループは共有ページフレームワークによって準備されます。`UILaunchPage::create_screen()` はホームコンテンツコンテナを埋めるだけで、一度だけ実行されます。

```cpp
void UILaunchPage::create_screen()
{
    if (carousel_elements[kCardCenter])
        return;

    create_app_container(content_container());
}
```

ここでホームカルーセル領域を作成します。5 枚のカード、5 つのタイトル、5 つのページドット、左右の矢印です。左上ロゴ、WiFi インジケータ、時刻ラベル、バッテリーバーは `home_base::creat_Top_UI()` が作成します。

### 6.2 Input Group Binding

ホーム入力グループは `AppPageRoot::input_group()` から得られます。`UILaunchPage::init_input_group()` はこれを互換ブリッジに保存し、アクティブなキーボード入力デバイスへバインドします。

```cpp
void UILaunchPage::init_input_group()
{
    ::home_input_group = input_group();
    bind_home_input_group();
}
```

これにより、キーボードイベントは `screen()` に配送され、LVGL コールバック `on_home_key()` が `handle_home_key()` へディスパッチして左右切り替えと Enter 起動を処理します。

### 6.3 Startup GIF and Home Display

`APPLAUNCH_STARTUP_ANIMATION` が有効で、プラットフォームが SDL ではない場合:

```text
Check cp0_file_path_c("logo_output.gif")
  -> file exists: UILaunchPage::start_startup_gif()
  -> file does not exist: UILaunchPage::load_home_screen()
```

`start_startup_gif()` は独立した GIF 画面を作成し、`this` とともにコールバックをバインドします。

```cpp
startup_gif_ = lv_gif_create(NULL);
lv_gif_set_src(startup_gif_, startup_gif_path_.data());
lv_obj_center(startup_gif_);
lv_obj_add_event_cb(startup_gif_, on_startup_gif_event, LV_EVENT_ALL, this);
lv_disp_load_scr(startup_gif_);
```

GIF 再生が終わると `LV_EVENT_READY` を受け取ります。`on_startup_gif_event()` は所有元の `UILaunchPage` インスタンスに戻り、`handle_startup_gif_event()` が GIF を一時停止してホーム画面を一度だけ読み込みます。

```cpp
if (event_code == LV_EVENT_READY && !startup_gif_done_) {
    startup_gif_done_ = true;
    if (startup_gif_) lv_gif_pause(startup_gif_);
    load_home_screen();
}
```

`load_home_screen()` の責務:

```cpp
show_home_screen();
cp0_signal_audio_api_play_asset("startup.mp3");
```

## 7. Boot Sequence Text

```text
main()
  -> cp0_lvgl_run(options)
      -> lv_init()
      -> cp0_lvgl_init()
      -> setup: register LV_EVENT_KEYBOARD
      -> setup: launcher_ui::init()
      -> new LauncherUiRuntime
          -> create_display()
              -> new LauncherFonts
              -> lv_disp_get_default()
              -> lv_theme_default_init()
          -> new Launch
          -> new UILaunchPage(Launch)
          -> Launch::set_launch_page()
          -> build_launcher_home()
              -> register LV_EVENT_GET_COMP_CHILD
              -> launch_page_->create_screen()
                  -> home_base::creat_Top_UI()
                  -> create_app_container(content_container())
              -> launch_->bind_ui()
                  -> new Launch
                  -> Register fixed/dynamic applications and write them into home slots
                  -> Create status bar and application directory watch timers
              -> launch_page_->init_input_group()
              -> load_home_screen() or start_startup_gif()
      -> lv_obj_invalidate(lv_screen_active())
      -> lv_refr_now(nullptr)
      -> lv_timer_handler() / semaphore wait
      -> teardown: launcher_ui::deinit()
      -> shared service shutdown
```

## 8. External Application Foreground Handoff

`Launch::launch_Exec()` は外部プロセスの実行前に screensaver foreground を下げ、入力 group を解除し、LVGL timer を停止します。その後 `cp0_signal_process_api({"ExecBlocking", ...})` を呼び、終了後に timer、ホーム入力 group、ホーム screen、foreground flag を復元します。フォアグラウンド切り替えはメインループの lock-file polling ではなく、この blocking 起動経路で完結します。

## 9. Notes

- `ui_init()` は内部ですでにホーム画面を作成し、読み込む場合があります。`main()` の後続の `lv_refr_now(NULL)` は最初のフレームの安全策であり、安易に削除しないでください。
- `cp0_lvgl_init()` は `ui_init()` より前に実行する必要があります。そうしないと `lv_disp_get_default()`、入力デバイス、パス、システムインターフェースが準備できていない可能性があります。
- SDL プラットフォームでは、起動 GIF はデフォルトでスキップされます。`logo_output.gif` を確認して再生するのはデバイス側だけです。
- ホーム入力は `UILaunchPage::bind_home_input_group()` を通して再バインドする必要があります。組み込みページや端末ページからホーム画面へ戻る場合も、このグループを復元する必要があります。
- 外部の独立アプリケーションが実行中は `LVGL_RUN_FLAGE=0` になります。この期間に APPLaunch が UI 更新を続けるとは想定しないでください。
- 外部アプリから復帰できない場合は、`ExecBlocking` callback の完了と `launch_Exec()` の timer/input/foreground 復元を調査してください。
