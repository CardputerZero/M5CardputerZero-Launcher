# 06 - リソースと設定システム

この章では、APPLaunch の実行時リソースディレクトリ、パス解決ルール、`.desktop` 動的アプリケーションファイル、設定 API、設定ページの設定キー、リソース利用時の注意点を説明します。主なソースファイルは `ext_components/cp0_lvgl/include/cp0_lvgl_app.h`、`ext_components/cp0_lvgl/src/cp0/cp0_lvgl_file.cpp`、`ext_components/cp0_lvgl/src/sdl/sdl_lvgl_file.cpp`、`projects/APPLaunch/main/ui/launch.cpp`、`projects/APPLaunch/main/ui/page_app/ui_app_setup.hpp` です。

## 1. リソースシステムの概要

APPLaunch のページコードでは、実行時パスを手動で連結しないでください。代わりに `cp0_file_path()` / `cp0_file_path_c()` を使い、統一されたルールで解決します。

```text
Page code / launch.cpp
        |
        v
img_path(), audio_path(), cp0_file_path_c()
        |
        v
cp0_lvgl_file.cpp / sdl_lvgl_file.cpp
        |
        +-- Images: share/images/...
        +-- Audio: /usr/share/APPLaunch/share/audio/...
        +-- Fonts: /usr/share/APPLaunch/share/font/...
        +-- applications: /usr/share/APPLaunch/applications
        +-- Special paths such as keyboard_device / keyboard_map / lock_file
```

ページ向けの共通ラッパー関数は `projects/APPLaunch/main/ui/ui_app_page.hpp` にあります。

```cpp
static inline std::string img_path(const char *name)
{
    return cp0_file_path(name);
}

static inline std::string audio_path(const char *name)
{
    return cp0_file_path(name);
}
```

## 2. 実行時リソースツリー

ソース側のリソースツリーは次の場所にあります。

```text
projects/APPLaunch/APPLaunch/
├── applications/
├── bin/
├── lib/
└── share/
    ├── audio/
    ├── font/
    └── images/
```

デバイスへインストールした後は、通常次の場所へ対応します。

```text
/usr/share/APPLaunch/
├── applications/
├── bin/
├── lib/
└── share/
    ├── audio/
    ├── font/
    └── images/
```

| Directory | 内容 | 使用箇所 |
| --- | --- | --- |
| `applications/` | `.desktop` アプリケーション記述子 | `Launch::applications_load()` |
| `share/images/` | アイコン、ステータスバー背景、ページ画像、GIF | ホーム画面、トップバー、組み込みページ |
| `share/audio/` | `startup.mp3`、`switch.wav`、`enter.wav`、ページ用キー音 | ホーム効果音、設定ページ、ページ効果音 |
| `share/font/` | TTF/OTF フォント | `LauncherFonts`、ページのカスタムフォント |
| `bin/` | パッケージ同梱スクリプトと外部プログラム | Store、更新スクリプト、動的アプリケーション |
| `lib/` | パッケージ同梱の動的ライブラリ | 外部プログラムまたはプラットフォーム機能 |

## 3. パス解決ルール

### 3.1 デバイス側 `cp0_lvgl_file.cpp`

デバイス側の実装は `ext_components/cp0_lvgl/src/cp0/cp0_lvgl_file.cpp` にあり、ルートディレクトリは固定です。

```cpp
constexpr const char *kAppRoot = "/usr/share/APPLaunch";
```

主なルール:

| Input | Output |
| --- | --- |
| `applications` | `/usr/share/APPLaunch/applications` |
| `lock_file` | `/tmp/M5CardputerZero-APPLaunch_fcntl.lock` |
| `keyboard_device` | `/dev/input/by-path/platform-3f804000.i2c-event` |
| `keyboard_map` | `/usr/share/keymaps/tca8418_keypad_m5stack_keymap.map` |
| `*.png` / `*.gif` / `*.jpg` / `*.jpeg` / `*.svg` | `share/images/<file>` |
| `*.wav` / `*.mp3` / `*.ogg` | `/usr/share/APPLaunch/share/audio/<file>` |
| `*.ttf` / `*.otf` | `/usr/share/APPLaunch/share/font/<file>` |
| その他の文字列 | そのまま返す |

現在のデバイス側画像ルールは `share/images/<file>` のような相対パスを返します。一方、音声とフォントは `/usr/share/APPLaunch/...` 配下の絶対パスを返します。ページコードでは既存の `img_path("xxx.png")` という慣例に従い、複数のルートディレクトリを混在させないでください。

### 3.2 SDL 実装 `sdl_lvgl_file.cpp`

SDL 実装は `ext_components/cp0_lvgl/src/sdl/sdl_lvgl_file.cpp` にあります。ルールはデバイス側とほぼ同じですが、ルートは `get_app_root_path()` で決まり、`app_relative_path(root_path, file, "share/images/")` などの関数が開発マシン上の実行ディレクトリに合わせて調整します。

特殊名は SDL 側でも維持されます。

```cpp
if (file == "applications") return root_path + "/applications";
if (file == "keyboard_device") return "/dev/input/by-path/platform-3f804000.i2c-event";
if (file == "keyboard_map") return "/usr/share/keymaps/tca8418_keypad_m5stack_keymap.map";
```

注意: SDL モードは、APPLaunch を開発マシンで実行できるようにするためのものです。すべてのデバイスリソースが存在するという意味ではありません。たとえば camera、LoRa、一部の Linux デバイスページは、コンパイル時条件で除外されるか、実行時に利用できない場合があります。

### 3.3 `cp0_file_path_c()` のキャッシュ

C インターフェースは `ext_components/cp0_lvgl/include/cp0_lvgl_app.h` で宣言されています。

```c
const char *cp0_file_path_c(const char *file);
```

実装では `thread_local std::unordered_map<std::string, std::string>` キャッシュを使います。

```cpp
extern "C" const char *cp0_file_path_c(const char *file)
{
    static thread_local std::unordered_map<std::string, std::string> paths;
    std::string key = file ? std::string(file) : std::string();
    auto it = paths.find(key);
    if (it == paths.end()) {
        it = paths.emplace(key, cp0_file_path(key)).first;
    }
    return it->second.c_str();
}
```

そのため、返される `const char *` は同じスレッド内では安定しており、LVGL の style API や image API に直接渡せます。スレッドをまたいでポインタを保存する場合は、代わりに `std::string` として保存してください。

## 4. 画像、音声、フォントの使用例

### 4.1 画像

ホーム画面や組み込みページでの一般的な使用例:

```cpp
{{"GAME", "game_100.png", "app_Game", false, true},
 nullptr, false, true, false, append_page_app<UIGamePage>},

lv_obj_set_style_bg_img_src(time_panel_,
    cp0_file_path_c("status_time_background.png"),
    LV_PART_MAIN | LV_STATE_DEFAULT);
```

ホームカードのアイコンは `launch.cpp::panel_set_icon()` で設定されます。

```cpp
static void panel_set_icon(lv_obj_t *panel, const char *src)
{
    lv_obj_t *img = lv_obj_get_child(panel, 0);
    if (!img || !lv_obj_check_type(img, &lv_image_class)) {
        img = lv_image_create(panel);
        lv_obj_set_size(img, LV_PCT(100), LV_PCT(100));
        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_STRETCH);
    }
    lv_image_set_src(img, src);
}
```

注意: `panel_set_icon()` は `access(icon_src, R_OK)` を確認してログを書きます。デバイス側の画像パスが相対パスの場合、実行時のカレントディレクトリが正しくなければ、ログには画像が存在しない、または読めないと出ます。

### 4.2 音声

ホームの効果音は、アセット名をシステム音声シグナルへ渡して再生します。

```cpp
static void audio_play_ui_asset(const char *name)
{
    cp0_signal_system_play_asset(name);
}

static void audio_play_switch(void) { audio_play_ui_asset("switch.wav"); }
static void audio_play_enter(void)  { audio_play_ui_asset("enter.wav"); }
```

起動音はホーム画面の読み込み後に再生されます。

```cpp
cp0_signal_audio_api_play_asset("startup.mp3");
```

ページ内でファイルパスが必要な場合は `audio_path("key_enter.wav")` を使います。下位 API がパスではなくアセット名を期待している場合は、二重にパス解決しないよう、アセット名を直接渡してください。

### 4.3 フォント

ホーム画面とトップバーは `LauncherFonts` で freetype フォントを管理します。

```cpp
launcher_fonts().get("Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD)
```

フォントパスは最終的に `cp0_file_path()` により `share/font/` へ解決されます。freetype フォントの読み込みに失敗した場合、`LauncherFonts` は LVGL 内蔵の Montserrat フォントへフォールバックします。

## 5. `.desktop` 動的アプリケーション

動的アプリケーションファイルは `cp0_file_path("applications")` が指すディレクトリに置きます。`Launch::applications_load()` は `*.desktop` ファイルだけを処理し、`[Desktop Entry]` セクションを解析します。

対応しているキー:

| key | 必須 | 説明 |
| --- | --- | --- |
| `Name` | Yes | カルーセルに表示する名前 |
| `Exec` | Yes | 起動コマンドまたは実行ファイルパス |
| `Icon` | No | アイコンパス。`share/images/...` または LVGL が読めるパスを指定可能 |
| `Terminal` | No | `true`/`True`/`1` の場合は `UISTPage` 内で実行 |
| `Sysplause` | No | ターミナルコマンド終了後に一時停止してユーザー確認を待つかどうか。既定は `true` |

例:

```ini
[Desktop Entry]
Name=Vim
TryExec=vim
Exec=vim
Terminal=true
Icon=share/images/e-Mail_80.png
Sysplause=true
```

読み込みルール:

- `[Desktop Entry]` セクション内の key-value ペアだけを読みます。
- 空行、および `#` または `;` で始まるコメントはスキップされます。
- `Name` または `Exec` のどちらかが欠けているエントリはスキップされます。
- `Exec` が既存アプリと重複する場合、そのエントリはスキップされます。
- `TryExec` は現在 `applications_load()` では使われていません。
- `applications/` ディレクトリは監視されます。3 秒ごとにポーリングされ、変更があると動的アプリをクリアしてディレクトリを再スキャンします。

## 6. 設定 API と永続化パス

現在の設定サービスは `cp0_signal_config_api` 経由で呼び出し、`Init`、`Save`、`GetInt`、`SetInt`、`GetStr`、`SetStr` をサポートします。サービス本体は `ext_components/cp0_lvgl/src/cp0_config_service.cpp` にあり、デバイス版と SDL 版はそれぞれ `ext_components/cp0_lvgl/src/cp0/cp0_lvgl_config.cpp`、`ext_components/cp0_lvgl/src/sdl/sdl_lvgl_config.cpp` で signal を登録します。

典型的な読み取り:

```cpp
int value = default_value;
cp0_signal_config_api(
    {"GetInt", key, std::to_string(default_value)},
    [&](int code, std::string data) {
        if (code == 0) value = std::stoi(data);
    });
```

書き込みは `SetInt` / `SetStr` の後に `Save` を呼びます。現在の設定ページとアプリ登録処理は、保存に失敗した場合に以前の値を復元します。

```cpp
cp0_signal_config_api({"SetInt", key, std::to_string(value)}, set_callback);
cp0_signal_config_api({"Save"}, save_callback);
```

読み取り時は常にデフォルト値を指定してください。`SetInt` / `SetStr` はメモリ上の項目だけを更新し、`Save` が成功して初めて永続化が完了します。

| 実行環境 | パスの選択規則 |
| --- | --- |
| デバイス | `$HOME/.config/cardputerzero/config.json`。`HOME` が空の場合は `/root/.config/cardputerzero/config.json` |
| SDL | `$APPLAUNCH_SDL_CONFIG_DIR/config.json`、`$XDG_CONFIG_HOME/applaunch-sdl/config.json`、`$HOME/.config/applaunch-sdl/config.json` の順。いずれも利用できない場合はランタイム data ディレクトリ配下の `sdl_config/config.json` |

どちらのバックエンドも一時ファイルへ書き込み、`fsync()` 後に正式ファイルへ rename します。

## 7. 設定キー一覧

### 7.1 Launcher アプリケーショントグル

`UISetupPage` の `Launcher` メニューは `app_<Name>` を保存します。

| Configuration key | Default | 意味 | 備考 |
| --- | --- | --- | --- |
| `app_Python` | `1` | Python エントリ | 常時有効、無効化不可 |
| `app_Store` | `1` | Store エントリ | 常時有効、無効化不可 |
| `app_CLI` | `1` | CLI エントリ | 常時有効、無効化不可 |
| `app_Game` | `1` | GAME エントリ | 常時有効、無効化不可 |
| `app_Setting` | `1` | SETTING エントリ | 常時有効、無効化不可 |
| `app_Math` | `1` | Calculator 外部アプリケーション | 設定可能 |
| `app_LoRa` | `1` | LORA 組み込みページ | 設定可能。すべてのビルドターゲットで登録 |
| `app_IP_Panel` | `1` | IP_PANEL 組み込みページ | 設定可能。Linux 非 SDL ビルドのみ登録 |
| `app_SSH` | `1` | SSH 組み込みページ | 設定可能。Linux 非 SDL ビルドのみ登録 |
| `app_Tank` | `1` | TANK 組み込みページ | 設定可能。Linux 非 SDL ビルドのみ登録 |

これらは `projects/APPLaunch/main/ui/builtin_app_registry.cpp` の `BUILTIN_APPS[]` に基づきます。`configurable=false` または `always_on=true` の項目は常に有効で、それ以外は設定 key がない場合に既定で有効です。

### 7.2 システムとページ設定

| Configuration key | 読み書き箇所 | 意味 |
| --- | --- | --- |
| `brightness` | `UISetupPage`, `ext_components/cp0_lvgl/src/commount.cpp` | バックライト輝度値。起動時に復元され、設定ページから書き込まれる |
| `volume` | `UISetupPage`, `commount.cpp` | システム音量。起動時に復元され、設定ページから書き込まれる |
| `dark_time` | `UISetupPage` | 画面オフタイムアウト。選択肢は `0/10/30/60/300` 秒 |
| `bt_named_only` | Bluetooth 設定 | 名前のある Bluetooth デバイスだけを表示するか。既定値は `1` |
| `run_as_user` | `cp0_process_commands.cpp`, `cp0_sudo_async.cpp` | 外部プロセス / PTY コマンドで権限を下げる際のユーザー設定 |

`extport_usb` と `extport_5vout` は `cp0_signal_settings_api` に渡す GPIO 名であり、設定ファイルの key ではありません。

### 7.3 一時的な業務入力

次のものは主にメモリ上のページ状態で、既定では永続化されません。

- `UISSHPage` の Host/Port/User のデフォルト値はコンストラクタで初期化され、設定には書き込まれません。
- `UIMeshPage` のメッセージ入力バッファはページメモリ内にだけ存在します。
- `UIIpPanelPage` のネットワークインターフェース一覧は `cp0_network_list()` から毎秒更新されます。

## 8. 設定ページの書き込み経路

`UISetupPage` は設定キーが集中している場所です。典型的な関数:

- `menu_init()`: 各設定 controller を通じて設定メニューを構築します。
- `launcher_app_registry_set_enabled()`: 設定可能な `app_*` を保存し、失敗時は以前の値を復元します。
- `Screen::apply_value()`: 輝度と画面オフタイムアウトを適用、保存します。
- `Speaker::apply_value()`: システム音量を書き込み、`volume` を保存します。
- `Bluetooth::toggle_named_only()`: `bt_named_only` を保存します。

例:

```cpp
bool launcher_app_registry_set_enabled(const AppDescriptor &desc, bool enabled)
{
    if (desc.always_on || !desc.configurable) return true;
    // SetInt、Save。いずれかが失敗した場合は以前の値を復元する。
}
```

設定キーを変更するときは、次のすべてを同期して確認してください。

- `BUILTIN_APPS[]` の `AppDescriptor` エントリ。
- `launcher_app_registry_entries()` を使用する Launcher 設定 controller。
- `app_registry.cpp` の `launcher_app_registry_is_enabled()` / `launcher_app_registry_set_enabled()`。
- ドキュメントとデフォルト設定。

## 9. リソース命名の推奨事項

- ホームアイコンは `game_100.png` や `setting_100.png` のように `<app>_100.png` と命名します。
- 小さなアイコンやステータス背景は、`status_time_background.png` や `status_battery_background.png` のように機能で命名します。
- ページ固有リソースには `setting_ok.png` や `setting_cross.png` のようにページ接頭辞を付けます。
- 効果音は `switch.wav`、`enter.wav`、`key_back.wav` のように短い名前を使います。
- フォントは `Montserrat-Bold.ttf` のように実ファイル名を使い、`launcher_fonts().get()` で読み込みます。

## 10. よくある問題と注意事項

- 画像と音声の拡張子は分類時に小文字化されますが、ファイルシステム自体は大文字小文字を区別するため、ファイル名そのものは一致している必要があります。
- `cp0_file_path()` は拡張子だけで分類し、ファイルが存在するかは確認しません。
- `.desktop` の `Icon` 値は自動的に `cp0_file_path()` を呼びません。LVGL が直接読めるパスにするか、既存テンプレートと整合させてください。
- デバイス側で新しいリソースを使う場合、パッケージングスクリプトが `projects/APPLaunch/APPLaunch/share/...` をインストールパッケージへ含めていることを確認してください。
- `SetInt` / `SetStr` の後に `Save` が成功しなければ、再起動後に値が失われます。
- `app_*` の変更は Launcher にアプリ一覧の再構築を通知します。常時有効の項目はトグル書き込みの影響を受けません。
- `run_as_user` は外部プロセスと PTY コマンドの実行ユーザーに影響します。権限問題をデバッグするときはこの設定を確認してください。
