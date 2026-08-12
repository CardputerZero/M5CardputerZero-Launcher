# 11 - デバッグとトラブルシューティング

この章では、APPLaunch の開発中およびデバイス配備時によくある問題を扱います。基本方針として、UI、アセット、入力ロジックの問題はまず SDL2 ビルドで再現し、その後デバイスログで framebuffer、evdev、権限、systemd の問題を切り分けます。

## 1. よく使うデバッグコマンド

### 1.1 リポジトリとビルド状態を確認

```bash
cd /home/nihao/w2T/github/launcher

git status --short
find docs/launcher工程详细说明 -maxdepth 1 -type f | sort
find projects/APPLaunch/APPLaunch -maxdepth 3 -type f | sort | sed -n '1,160p'
```

### 1.2 SDL2 をローカルでビルドして実行

```bash
cd /home/nihao/w2T/github/launcher/projects/APPLaunch
CONFIG_DEFAULT_FILE=linux_x86_sdl2_config_defaults.mk scons -j8 --implicit-deps-changed
./dist/M5CardputerZero-APPLaunch
```

用途:

- ホームページ、組み込みページ、カルーセルアニメーション、`.desktop` スキャンを素早く確認する。
- LVGL オブジェクト作成とアセットパスを確認する。
- デバイス側 framebuffer、evdev、systemd 権限問題を避ける。

### 1.3 デバイス側 / クロスビルド

```bash
cd /home/nihao/w2T/github/launcher/projects/APPLaunch
CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk scons -j8 --implicit-deps-changed
```

デバイス上でネイティブビルドする場合:

```bash
cd /path/to/launcher/projects/APPLaunch
CONFIG_DEFAULT_FILE=config_defaults.mk scons -j4 --implicit-deps-changed
```

### 1.4 APPLaunch 実行時ログを見る

systemd で起動している場合:

```bash
systemctl --user status APPLaunch.service --no-pager
journalctl --user -u APPLaunch.service -b --no-pager
journalctl --user -u APPLaunch.service -f
```

デバイスバイナリを手動実行する場合:

```bash
systemctl --user stop APPLaunch.service
cd /usr/share/APPLaunch
sudo /usr/share/APPLaunch/bin/M5CardputerZero-APPLaunch 2>&1 | tee /tmp/applaunch.log
```

実際のバイナリパスはパッケージングとインストール方法に依存します。ビルド出力 `projects/APPLaunch/dist/M5CardputerZero-APPLaunch` の場合もあります。

### 1.5 実行時アセットを確認

```bash
ls -l /usr/share/APPLaunch
find /usr/share/APPLaunch/share/images -maxdepth 1 -type f | sort | sed -n '1,120p'
find /usr/share/APPLaunch/share/audio -maxdepth 1 -type f | sort
find /usr/share/APPLaunch/share/font -maxdepth 1 -type f | sort
find /usr/share/APPLaunch/applications -maxdepth 1 -type f | sort
```

### 1.6 入力デバイスを確認

```bash
ls -l /dev/input/by-path/
ls -l /dev/input/event*
sudo evtest
```

コード内のデフォルトキーボードデバイス:

```text
/dev/input/by-path/platform-3f804000.i2c-event
```

環境変数で上書きできます。

```bash
APPLAUNCH_LINUX_KEYBOARD_DEVICE=/dev/input/eventX sudo /usr/share/APPLaunch/bin/M5CardputerZero-APPLaunch
```

### 1.7 設定ファイルを確認

```bash
sudo ls -l /var/lib/applaunch
sudo cat /var/lib/applaunch/settings
```

よく使う設定キー:

- `app_Game`, `app_Math`, `app_File`, `app_Camera` など: Launcher ページ表示トグル。
- `brightness`: 輝度。
- `volume`: 音量。
- `dark_time`: 画面オフタイムアウト。
- `cam_resolution`: カメラ解像度。
- `startup_mode`: 起動モード。
- `extport_usb`, `extport_5vout`: 拡張ポート設定。
- `run_as_user`: 外部プロセスの権限を下げるときに使うユーザー。

## 2. ログキーワード早見表

| Keyword | Location | Meaning |
| --- | --- | --- |
| `[BOOT] lv_init() done` | `main.cpp` | LVGL 初期化完了 |
| `[BOOT] cp0_lvgl_init() starting...` | `main.cpp` | プラットフォーム適応層、表示、入力、音声などの初期化開始 |
| `[BOOT] First frame flushed to fb0.` | `main.cpp` | 最初のフレームを表示デバイスへ強制 flush 済み |
| `Entering main loop` | `main.cpp` | メインループ開始 |
| `[LAUNCHER] set panel icon` | `launch.cpp` | ホームアイコン設定成功 |
| `set panel icon missing/unreadable` | `launch.cpp` | アイコンパスが存在しない、または読めない |
| `applications_load: opendir failed` | `launch.cpp` | applications ディレクトリが存在しない、または読めない |
| `missing Name or Exec` | `launch.cpp` | `.desktop` に必須フィールドがない |
| `duplicate Exec` | `launch.cpp` | `.desktop` の Exec が既存アプリと同じ |
| `Launching terminal app` | `launch.cpp` | コマンド実行のため組み込みターミナルページへ入る |
| `Launching external app` | `launch.cpp` | 非ターミナル外部プログラムを開始 |
| `[CP0-APP] ESC DOWN/UP` | `cp0_lvgl_process.cpp` | 外部アプリ実行中に親プロセスが ESC を読んだ |
| `[cp0] Returned to launcher` | `cp0_lvgl_process.cpp` | 外部アプリが終了し、ホームへ戻る準備中 |
| `[HOME_STATUS] connected=` | `launch.cpp` | ホームステータスバーの WiFi/battery 状態を更新 |

## 3. 黒画面のトラブルシューティング

黒画面では、まずプロセスが起動していないのか、LVGL が最初のフレームを flush していないのか、アセット/ページ構築でクラッシュしたのか、外部アプリが framebuffer を占有しているのか、バックライト/輝度が間違っているのかを判断します。

### 3.1 プロセス状態を素早く確認

```bash
pgrep -a M5CardputerZero-APPLaunch
systemctl --user status APPLaunch.service --no-pager
journalctl --user -u APPLaunch.service -b --no-pager | tail -120
```

プロセスがない場合:

- systemd unit の `ExecStart` パスが存在するか確認します。
- バイナリに実行権限があるか確認します。
- バイナリを手動実行して stderr を確認します。

プロセスが繰り返し再起動している場合:

```bash
journalctl --user -u APPLaunch.service -b --no-pager | grep -Ei 'segfault|assert|error|failed|No such|permission'
```

### 3.2 起動ログの停止段階を確認

停止位置により調査方向が変わります。

| Stop point | Possible cause | Troubleshooting direction |
| --- | --- | --- |
| No `[BOOT] lv_init()` | プログラムが実行されていない、または非常に早期にクラッシュ | systemd、バイナリパス、動的ライブラリ、権限 |
| Stops at `cp0_lvgl_init() starting` | 表示/入力/音声/ハードウェア初期化で停止 | framebuffer、evdev、音声デバイス、hardware HAL |
| `ui_init done` appears but screen is black | first-frame flush 失敗、バックライト 0、アセットによりオブジェクト不可視 | framebuffer、バックライト、アセットパス |
| Black after entering main loop | ページ描画問題、または外部アプリが表示をロック | ログ、ロックファイル、外部プロセス |

### 3.3 Framebuffer とバックライトを確認

```bash
ls -l /dev/fb0
id
sudo cat /sys/class/backlight/backlight/brightness
sudo cat /sys/class/backlight/backlight/max_brightness
```

輝度を上げてみます。

```bash
echo 80 | sudo tee /sys/class/backlight/backlight/brightness
```

Settings ページが以前に非常に低い輝度を保存していた場合は確認します。

```bash
sudo grep '^brightness=' /var/lib/applaunch/settings
```

### 3.4 外部アプリが表示を占有していないか確認

APPLaunch が非ターミナル外部アプリを起動すると、自身の LVGL タイマーを停止し、子プロセス終了を待ちます。外部アプリがハングすると、Launcher が黒画面または無反応のように見える場合があります。

```bash
ps -eo pid,ppid,pgid,stat,cmd | grep -E 'APPLaunch|Calculator|AppStore|my_app' | grep -v grep
```

まず外部アプリのプロセスグループを丁寧に終了するか、ESC を約 3 秒押し続けて `cp0_process_exec_blocking()` の SIGTERM を発火させます。

### 3.5 SDL2 で範囲を絞る

SDL2 ビルドが動くがデバイスビルドが黒い場合は、デバイス HAL、framebuffer、バックライト、evdev、権限、systemd を優先して調べます。SDL2 も黒い場合は、UI 構築、アセットパス、LVGL オブジェクトスタイルを優先します。

## 4. アセット欠落のトラブルシューティング

アセット欠落の典型症状は、空白アイコン、背景欠落、フォントフォールバック、音が出ない、ログの `missing/unreadable` です。

### 4.1 ソースと実行時ディレクトリの両方でアセットを確認

```bash
# Source tree
find projects/APPLaunch/APPLaunch/share/images -maxdepth 1 -type f | sort | grep my_icon

# Device side
find /usr/share/APPLaunch/share/images -maxdepth 1 -type f | sort | grep my_icon
```

ソースツリーにはあるがデバイスにない場合:

- 再ビルド、再パッケージ、再インストールします。
- `projects/APPLaunch/main/SConstruct` に `STATIC_FILES += [ADir('../APPLaunch')]` が残っているか確認します。
- パッケージングスクリプトが `APPLaunch/` アセットツリーをパッケージへコピーしているか確認します。

### 4.2 パス表記を確認

組み込みページでの推奨:

```cpp
img_path("my_icon_100.png")
audio_path("key_enter.wav")
```

`.desktop` での推奨:

```ini
Icon=share/images/my_icon_100.png
```

デバイス側ページに `/home/nihao/.../projects/APPLaunch/...` のような開発ホスト絶対パスを書かないでください。インストール後のデバイスアセットルートは `/usr/share/APPLaunch/` です。

### 4.3 画像パスの特殊ケースを理解する

デバイス上では、`cp0_file_path("xxx.png")` は `share/images/xxx.png` を返します。これはカレントディレクトリからの相対パスです。想定外のディレクトリからデバイスバイナリを手動起動すると、画像が見つからないことがあります。作業ディレクトリを `/usr/share/APPLaunch` にして実行するか、正しい systemd `WorkingDirectory` を使ってください。

SDL2 では APPLaunch が `APPLaunch/share` を自動探索しますが、それでもビルド出力は `projects/APPLaunch` から実行することを推奨します。

### 4.4 フォント欠落

フォントファイルを確認:

```bash
find /usr/share/APPLaunch/share/font -maxdepth 1 -type f | sort
```

フォント追加後にクラッシュする、または表示されない場合:

- 拡張子が `.ttf` または `.otf` であることを確認します。
- FreeType ビルド依存関係が利用可能であることを確認します。
- まず `Montserrat-Bold.ttf` や `AlibabaPuHuiTi-3-55-Regular.ttf` など既存フォントでページロジックを検証します。

## 5. 入力が動かない

入力不具合には、ホームページが反応しない、組み込みページが反応しない、外部アプリが反応しない、キーコードマッピングが間違っている、などがあります。

### 5.1 ホームページまたは組み込みページが反応しない

正しい入力グループがバインドされているか確認します。

- ホームページ: `UILaunchPage::bind_home_input_group()`。
- 組み込みページ: ページ作成後、`launch.cpp` が `lv_indev_set_group(lv_indev_get_next(NULL), p->input_group())` を呼びます。
- ホームへ戻る: `Launch::lv_go_back_home()` がホーム入力グループを再バインドします。

組み込みページにイベントを追加する場合、イベントが正しいオブジェクトに付けられており、そのオブジェクトがページ入力グループに属していることを確認してください。既存ページの `event_handler_init()` 実装を参照します。

### 5.2 デバイス evdev にイベントがあるか確認

```bash
ls -l /dev/input/by-path/platform-3f804000.i2c-event
sudo evtest /dev/input/by-path/platform-3f804000.i2c-event
```

デフォルトパスが存在しない場合、一時的に上書きします。

```bash
APPLAUNCH_LINUX_KEYBOARD_DEVICE=/dev/input/eventX sudo /usr/share/APPLaunch/bin/M5CardputerZero-APPLaunch
```

### 5.3 キーコードマッピング問題

関連ファイル:

| File | Purpose |
| --- | --- |
| `ext_components/cp0_lvgl/include/input_keys.h` | 互換入力キー定義 |
| `ext_components/cp0_lvgl/include/keyboard_input.h` | APPLaunch private input header |
| `ext_components/cp0_lvgl/include/keyboard_input.h` | cp0_lvgl input interface |
| `ext_components/cp0_lvgl/src/cp0/cp0_lvgl_keyboard.c` | デバイス側キーボード入力実装 |
| `ext_components/cp0_lvgl/src/sdl/sdl_lvgl_keyboard.c` | SDL2 キーボード入力実装 |

調査方法:

- SDL2 でまず矢印キー、Enter、Esc が期待どおり動くか確認します。
- デバイスでは `evtest` で生のキーコードを読みます。
- `LV_KEY_*` とプロジェクト独自キー値を比較します。
- 外部アプリ実行中は `[CP0-APP] evdev code=... value=...` ログを確認します。

### 5.4 外部アプリの入力が動かない

非ターミナル外部アプリ起動後、APPLaunch は `keyboard_pause()` で自身のキーボードスレッドを一時停止しますが、デバイスを EVIOCGRAB はしません。親子両方のプロセスが同じ evdev デバイスを読めます。外部アプリに入力がない場合:

- 外部アプリが同じ `/dev/input/event*` を読んでいるか確認します。
- 実行時ユーザーにその入力デバイスを読む権限があるか確認します。外部アプリは既定で root から通常ユーザーへ下げられる場合があります。
- `run_as_user` 設定を確認するか、`run_as_root` を使う固定組み込み登録を使います。
- まず `Terminal=true` で、コマンドが PTY 内でキーボード入力を受け取れるか確認します。

## 6. 外部アプリから戻れない

外部アプリから戻れない原因は、多くの場合、子プロセスが終了しない、プロセスグループが kill されない、ESC が入力デバイスから読めない、またはアプリが表示を奪ったまま復元しないことです。

### 6.1 通常の復帰経路

`launch.cpp` の `launch_Exec()`:

1. Loading を表示します。
2. `LVGL_RUN_FLAGE = 0` を設定します。
3. LVGL 入力グループを解除します。
4. `lv_timer_enable(false)` で LVGL タイマーを一時停止します。
5. `cp0_process_exec_blocking(exec, keep_root)` を呼びます。ESC 状態は原子的な `cp0_esc_state_*` API で受け渡します。
6. 子プロセス終了後、タイマーを再有効化し、ホーム入力グループをバインドし、`launch_page_->show_home_screen()` でホーム画面を復元し、Loading を非表示にします。

### 6.2 まず子プロセスがまだ実行中か確認

```bash
ps -eo pid,ppid,pgid,stat,cmd | grep -E 'APPLaunch|my_app|sh -c' | grep -v grep
```

子プロセスがまだ動いている場合:

- アプリ自身に終了させます。
- ESC を約 3 秒押し続けます。
- ログに `[cp0] ESC held ... SIGTERM pgid ...` が含まれるか確認します。

### 6.3 ESC 長押しが効かない

確認:

```bash
journalctl --user -u APPLaunch.service -f | grep -E 'CP0-APP|ESC|Returned'
```

`[CP0-APP] evdev` ログがない場合:

- デフォルトキーボードパスが間違っている可能性があります。
- 親プロセスに入力デバイス権限がない可能性があります。
- 外部アプリまたは別プロセスが入力デバイスを排他的に使用している可能性があります。

ESC DOWN はあるが SIGTERM がない場合:

- 長押し時間が足りません。現在のしきい値は 3 秒です。
- キーコードが `KEY_ESC` ではありません。キーボードマッピングを確認します。

### 6.4 子は終了したがホームページが復元しない

ログに次が含まれるか確認します。

```text
[cp0] Returned to launcher
App ... exited with code ...
```

復帰ログがあるのに画面が異常な場合:

- 外部アプリが framebuffer 状態または terminal mode を変更した可能性があります。
- APPLaunch はホーム切替後に `lv_obj_invalidate()` と `lv_refr_now()` で強制更新を試みています。それでも表示されない場合は framebuffer/backlight を確認します。
- 外部アプリがロックやバックグラウンドプロセスを残し、表示を占有し続けていないか確認します。

## 7. ビルド失敗のトラブルシューティング

### 7.1 SCons が SDK またはコンポーネントを見つけられない

症状: `project.py`、components、headers が見つからない。

調査:

```bash
cd /home/nihao/w2T/github/launcher/projects/APPLaunch
python3 - <<'PY'
from pathlib import Path
p = Path.cwd()
print('cwd=', p)
print('SDK=', p.parent.parent / 'SDK')
print('ext=', p.parent.parent / 'ext_components')
PY
ls ../../SDK/tools/scons/project.py
ls ../../ext_components/cp0_lvgl
```

APPLaunch の `SConstruct` は自動的に次を設定します。

```python
SDK_PATH = ../../SDK
EXT_COMPONENTS_PATH = ../../ext_components
```

### 7.2 SDL2 / FreeType / libinput 依存関係不足

SDL2 設定は `pkg-config` で `sdl2` と `freetype2` を探し、`input`、`xkbcommon`、`udev` をリンクします。

確認:

```bash
pkg-config --cflags --libs sdl2 freetype2
ldconfig -p | grep -E 'libinput|libxkbcommon|libudev'
```

一般的な Ubuntu/Debian 依存パッケージ名:

```bash
sudo apt-get install scons pkg-config libsdl2-dev libfreetype-dev libinput-dev libxkbcommon-dev libudev-dev
```

### 7.3 クロスコンパイル sysroot 不足

`linux_x86_cross_cp0_config_defaults.mk` は `SDK/github_source/static_lib_v0.0.6` を sysroot として使います。存在しない場合、`SConstruct` は `sdk_bsp.tar.gz` のダウンロードを試みます。ネットワークアクセスが制限されていると失敗します。

確認:

```bash
ls -l ../../SDK/github_source/static_lib_v0.0.6
cat ../../SDK/github_source/static_lib_v0.0.6/version 2>/dev/null || true
```

ダウンロードに失敗する場合は、事前に sysroot を用意するか、ネットワークが利用できる環境で一度ビルドしてキャッシュを作成してください。

### 7.4 ページ追加後にビルドが失敗する

よくある原因:

| Symptom | Cause | Fix |
| --- | --- | --- |
| `PageT not declared` | ページクラス名と登録名が一致しない、または `.hpp` が `generated/page_app.h` に include されていない | `generated/page_app.h` を確認し、scons を再実行 |
| SDL2 build cannot find Linux headers | ページがデバイス専用ヘッダーを直接 include している | デバイス専用コードを `#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)` で囲む |
| Linker cannot find symbols | 新しいページが呼ぶ関数がコンポーネント依存関係に追加されていない | `main/SConstruct` の `REQUIREMENTS`/`LDFLAGS` を確認 |
| Duplicate definition | header-only ページが非 inline のグローバル変数/関数を定義している | クラスメンバー、`static`、`inline` にするか `.cpp` へ移動 |

### 7.5 `generated/page_app.h` 自動生成で作業ツリーが変わる

`generate_page_app_includes.py` はファイル名順に `generated/page_app.h` を生成します。`page_app/*.hpp` を追加または削除した後にビルドすると、このファイルが変更される場合があります。これは期待される挙動ですが、コミット前に diff が意図した include-list 変更だけであることを確認してください。

## 8. `.desktop` 読み込み失敗のトラブルシューティング

### 8.1 ファイルがスキャンされない

確認:

```bash
ls -l /usr/share/APPLaunch/applications
```

要件:

- ファイル名が `.desktop` で終わる。
- 内容に `[Desktop Entry]` を含む。
- 少なくとも `Name=` と `Exec=` が必要。
- 空行と `#` または `;` で始まるコメントはスキップされる。

### 8.2 重複排除でアプリがスキップされた

ログに次が含まれる場合:

```text
applications_load: skip ... (duplicate Exec)
```

既存の固定アプリまたは別の `.desktop` が同じ `Exec` を使っています。`Exec` を一意のコマンドに変更してください。

### 8.3 アイコンが表示されない

`.desktop` の `Icon` フィールドは自動的に `img_path()` を呼ばず、そのまま `panel_set_icon()` へ渡されます。そのため、次を使います。

```ini
Icon=share/images/my_app_100.png
```

絶対パスを使う場合も、デバイス上にファイルが存在し読めることを確認してください。

### 8.4 コマンド実行に失敗した

ターミナルアプリでは、まずコマンドラインで確認します。

```bash
which vim
vim --version
```

非ターミナルアプリでは次を確認します。

```bash
ls -l /usr/share/APPLaunch/bin/my_app
ldd /usr/share/APPLaunch/bin/my_app
sudo -u pi /usr/share/APPLaunch/bin/my_app
```

APPLaunch が root として起動する場合、外部アプリは通常、通常ユーザーへ権限を下げようとします。root が必要なアプリは `run_as_root` を使う固定組み込みエントリとして登録するか、不要な root 権限を避けられるようプログラム権限/グループ権限を調整してください。

## 9. 推奨される問題切り分け順序

1. `git status --short` を実行し、現在の変更範囲を確認します。
2. SDL2 をビルドして実行し、基本的な UI/構文問題を除外します。
3. アセットが `projects/APPLaunch/APPLaunch` とデバイス `/usr/share/APPLaunch` の両方に存在するか確認します。
4. `journalctl --user -u APPLaunch.service -f` を見て起動段階を特定します。
5. `evtest` で入力デバイスとキーコードを確認します。
6. `ps` で外部アプリとプロセスグループを確認します。
7. `/var/lib/applaunch/settings` を確認し、設定トグル、輝度、実行時ユーザー問題を除外します。
8. 最後に `ext_components/cp0_lvgl/src/cp0/` 配下の HAL 層を確認します。framebuffer、keyboard、process、settings、audio 実装が対象です。
