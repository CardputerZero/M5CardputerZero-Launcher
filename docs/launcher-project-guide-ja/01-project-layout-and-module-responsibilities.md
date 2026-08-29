# 01 - Project Layout and Module Responsibilities

この章では、リポジトリ全体の構成と、APPLaunch プロジェクト内部の構成を説明します。

## 1. Overall Repository Structure

```text
launcher/
├── SDK/
├── ext_components/
├── projects/
├── docs/
├── README.md
└── README_ZH.md
```

### 1.1 `SDK/`

`SDK` は `M5Stack_Linux_Libs` で、このプロジェクトに次のものを提供します。

- SCons/Kconfig ビルドフレームワーク。
- LVGL コンポーネント。
- デバイスドライバ、ユーティリティ関数、サンプルコード。
- ビルドスクリプトとコンポーネント登録機構。

APPLaunch の `SConstruct` は次を設定します。

```python
os.environ["SDK_PATH"] = str(sdk_path)
```

続いて次を呼び出します。

```python
env = SConscript(
    str(sdk_path / "tools" / "scons" / "project.py"),
    variant_dir=os.getcwd(),
    duplicate=0,
)
```

### 1.2 `ext_components/`

`ext_components` は、このリポジトリの拡張コンポーネントディレクトリです。APPLaunch は `EXT_COMPONENTS_PATH` を通してこれを取り込みます。

```text
ext_components/
├── cp0_lvgl/
├── Miniaudio/
├── RadioLib/
└── Sigslot/
```

| Component | Role |
| --- | --- |
| `cp0_lvgl` | CardputerZero プラットフォーム適応。LVGL 初期化、ファイルパス、入力、プロセス、PTY、システム機能をラップする |
| `Miniaudio` | 音声再生と録音の依存ライブラリ |
| `Sigslot` | signal-slot 機構 |
| `RadioLib` | LoRa/SX126x 無線通信ライブラリコンポーネント |

### 1.3 `projects/`

```text
projects/
├── APPLaunch/
├── AppStore/
├── Calculator/
├── CardputerZero-Emulator/
├── HelloWorld/
└── UserDemo/
```

| Project | Description |
| --- | --- |
| `APPLaunch` | メインランチャー。このドキュメントの主対象 |
| `AppStore` | アプリケーションストア。APPLaunch から外部アプリケーションとして起動できる |
| `Calculator` | 電卓アプリケーション。APPLaunch から起動できる |
| `CardputerZero-Emulator` | デバイスエミュレータ |
| `HelloWorld` | ビルドフロー学習用の最小サンプルプロジェクト |
| `UserDemo` | ユーザーデモプロジェクト |

### 1.4 `docs/` と `scripts/`

- `docs/`: 開発者向けドキュメントと単独のパッケージング文書。`APPLaunch-App-打包指南.md` などを含みます。
- `scripts/`: `firmware_manager.py` や `debian_packager.py` など、リポジトリレベルの補助ツール。

## 2. APPLaunch Top-Level Structure

```text
projects/APPLaunch/
├── APPLaunch/
├── main/
├── tools/
├── docs/
├── SConstruct
├── config_defaults.mk
├── linux_x86_sdl2_config_defaults.mk
├── linux_x86_cross_cp0_config_defaults.mk
├── mac_cross_cp0_config_defaults.mk
├── darwin_config_defaults.mk
└── setup.ini
```

### 2.1 Top-Level Build Files

| File | Description |
| --- | --- |
| `SConstruct` | プロジェクトのエントリポイント。デフォルト設定、SDK パス、クロスコンパイル sysroot を選び、SDK ビルドシステムを呼び出す |
| `config_defaults.mk` | デバイス上のデフォルト設定。Linux framebuffer / evdev を有効化する |
| `linux_x86_sdl2_config_defaults.mk` | Linux x86 SDL2 シミュレーション設定 |
| `linux_x86_cross_cp0_config_defaults.mk` | AArch64 向け Linux x86 クロスコンパイル設定 |
| `mac_cross_cp0_config_defaults.mk` | AArch64 向け macOS クロスコンパイル設定 |
| `darwin_config_defaults.mk` | macOS SDL / Darwin 関連設定 |

### 2.2 `APPLaunch/` Runtime Resource Tree

```text
projects/APPLaunch/APPLaunch/
├── applications/
│   └── vim.desktop.temple
├── lib/
│   └── nihao.so
└── share/
    ├── audio/
    ├── font/
    └── images/
```

このディレクトリは、ビルドまたはパッケージ作成時に実行時ディレクトリへコピーされます。デバイスにインストールされた後は、通常次に対応します。

```text
/usr/share/APPLaunch/
```

リソースツリーの責務:

- `applications/`: 外部アプリケーション用の `.desktop` 記述ファイルを格納します。
- `share/images/`: アプリケーションアイコン、ホームカルーセル画像、ステータスバー画像、ページ画像。
- `share/audio/`: 起動音、キー音、切り替え音。
- `share/font/`: TTF フォント。
- `lib/`: パッケージに同梱されるライブラリファイル。

### 2.3 `main/` Main Source Directory

```text
projects/APPLaunch/main/
├── Kconfig
├── SConstruct
├── include/
├── src/
└── ui/
```

| Path | Description |
| --- | --- |
| `Kconfig` | コンポーネント設定のエントリポイント |
| `SConstruct` | APPLaunch のビルドターゲットと依存関係を登録する |
| `include/` | APPLaunch のプライベートヘッダと互換ヘッダ |
| `src/main.cpp` | プロセスのエントリポイント。共有 `cp0_lvgl_run()` に APPLaunch の setup/teardown callback を渡す |
| `ui/` | すべての UI ページ、ホーム画面、アニメーション、Loading などの実装 |

### 2.4 `main/ui/` UI Directory

```text
main/ui/
├── ui.cpp / ui.h
├── launch.cpp / launch.h
├── builtin_app_registry.cpp / builtin_app_registry.hpp
├── desktop_app_loader.cpp / desktop_app_loader.hpp
├── ui_launch_page.cpp / ui_launch_page.h
├── launcher_ui_app_page.hpp
├── generated/page_app.h
├── generate_page_app_includes.py
├── ui_loading.*
├── ui_global_hint.*
├── launcher_ui_runtime.*
├── animation/
└── page_app/
```

| File/Directory | Role |
| --- | --- |
| `ui.c` / `ui.cpp` / `ui.h` | UI 初期化、グローバルオブジェクト、C/C++ ブリッジ |
| `launch.cpp` | アプリケーションリスト、起動、ページ lifetime、ホームカルーセルを管理する |
| `builtin_app_registry.cpp` | 固定エントリの descriptor、表示可否、組み込みページ/外部コマンドの登録 |
| `desktop_app_loader.cpp` | `.desktop` の読み込み、重複除外、ディレクトリ watcher |
| `ui_launch_page.cpp` | ホーム UI 作成、カルーセルスロット、キー処理、起動アニメーション |
| `ui_loading.cpp` | Loading オーバーレイ |
| `ui_global_hint.cpp` | グローバルヒント |
| `launcher_ui_runtime.cpp` | テーマ、`Launch` / `UILaunchPage` の所有、ホーム構築 |
| `animation/` | ホームカルーセルアニメーションの実装 |
| `components/` | ページ基底クラス、コンポーネント、カスタムページ |

### 2.5 `page_app/` Built-In Page Directory

```text
main/ui/page_app/
├── ui_app_st.{hpp,cpp,...}
├── ui_app_game.{hpp,cpp,...}
├── ui_app_setup.{hpp,cpp,...}
├── ui_app_lora.{hpp,cpp,...}
├── ui_app_ssh.{hpp,cpp,...}
├── ui_app_tank_battle.{hpp,cpp,...}
└── ui_app_ip_panel.{hpp,cpp,...}
```

ページは header と複数の `.cpp` に分割されています。`generate_page_app_includes.py` は header を `generated/page_app.h` へ集約しますが、登録の有無は `builtin_app_registry.cpp` で決まります。

## 3. Module Dependencies

簡略化した依存関係グラフ:

```text
main.cpp
└── cp0_lvgl_run(options)
    ├── lv_init() / cp0_lvgl_init() / LVGL loop
    ├── setup -> launcher_ui::init() / ui_screensaver_init()
    └── teardown -> launcher_ui::deinit()

ui_init()
  ├── UILaunchPage
  ├── Launch
  ├── ui_loading
  └── page_app/*

Launch
├── builtin_app_registry -> append_page_app<PageT> / external command
├── desktop_app_loader -> `.desktop` scan / directory watcher
├── UILaunchPage -> carousel / home screen
└── cp0_signal_process_api() -> external process
```

## 4. Code Style Characteristics

APPLaunch には現在、いくつかの明確なコードスタイル上の特徴があります。

- C と C++ が混在しています。LVGL 生成コードや互換コードは C であることが多く、ほとんどの業務ページは C++ です。
- LVGL コールバックは C スタイルの static 関数のままですが、ページディスパッチでは `lv_event_get_user_data()` を使って所有元の C++ ページインスタンスを復元します。
- ページクラスは通常、追加の UI フレームワークを使わず、LVGL オブジェクトを直接構築します。
- ハードウェア機能には、できるだけ `cp0_lvgl` がラップした統一インターフェース経由でアクセスします。
- リソースアクセスでは、デバイス環境と SDL 環境のパス差を避けるため、できるだけ `cp0_file_path()` を使用します。
