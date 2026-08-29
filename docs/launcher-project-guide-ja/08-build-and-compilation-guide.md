# 08 - ビルドとコンパイルガイド

この章では、`projects/APPLaunch` の完全なビルド手順を説明します。Linux SDL2 ネイティブシミュレーション、デバイス上のネイティブビルド、Linux x86 クロスコンパイル、macOS クロスコンパイル、Windows SDL2/クロスビルド、依存関係のインストール、環境変数、主要な SCons ロジック、よくあるエラーへの対処を扱います。

特に記載がない限り、すべてのコマンドはリポジトリルートから開始するものとします。

```bash
cd /home/nihao/w2T/github/launcher
```

## 1. ビルドターゲット概要

APPLaunch は複数の形式でビルドできます。中核的な違いは、`CONFIG_DEFAULT_FILE` が指す設定ファイルで決まります。

| Build target | 実行場所 | Configuration file | 表示/入力バックエンド | 典型的な用途 |
| --- | --- | --- | --- | --- |
| Linux SDL2 native simulation | Linux x86_64 開発マシン | `linux_x86_sdl2_config_defaults.mk` | SDL2 window + SDL input | 日常的な UI デバッグと高速開発 |
| Native device build | M5CardputerZero AArch64 Linux | `config_defaults.mk` | Linux framebuffer + evdev | デバイス上で直接ビルドして実行 |
| Linux x86 cross-compilation | Linux x86_64 開発マシン、出力はデバイスで実行 | `linux_x86_cross_cp0_config_defaults.mk` | Linux framebuffer + evdev | 公式デバイス成果物の推奨ビルド方法 |
| macOS cross-compilation | macOS 開発マシン、出力はデバイスで実行 | `mac_cross_cp0_config_defaults.mk` | Linux framebuffer + evdev | macOS で arm64 デバイス成果物を生成 |
| macOS SDL/Darwin configuration | macOS 開発マシン | `darwin_config_defaults.mk` | SDL-related configuration | ネイティブ SDL 作業のベース設定 |
| Windows SDL2 native simulation | Windows x86_64 開発マシン | `win_x86_sdl2_config_defaults.mk` | SDL2 window + SDL input | Windows 上の UI デバッグ |
| Windows x86 cross-compilation | Windows x86_64 開発マシン、出力はデバイスで実行 | `win_x86_cross_config_defaults.mk` | Linux framebuffer + evdev | Windows で arm64 デバイス成果物を生成 |

ビルド成果物は通常次の場所に生成されます。

```text
projects/APPLaunch/dist/
├── M5CardputerZero-APPLaunch
└── APPLaunch/
    └── share/
```

各項目の意味:

- `M5CardputerZero-APPLaunch` はメイン実行ファイルです。
- `APPLaunch/` は実行時リソースツリーで、`dist/APPLaunch` にコピーされます。

## 2. 前提条件

### 2.1 サブモジュールとディレクトリ構成

初回 clone では次を使います。

```bash
git clone --recursive https://github.com/CardputerZero/launcher.git
cd launcher
```

リポジトリは clone 済みだがサブモジュールが未初期化の場合:

```bash
git submodule update --init --recursive
```

APPLaunch のトップレベル `SConstruct` は次のディレクトリ関係を前提にしています。

```text
launcher/
├── SDK/
├── ext_components/
└── projects/
    └── APPLaunch/
        ├── SConstruct
        └── main/SConstruct
```

ビルド前に APPLaunch プロジェクトディレクトリへ移動してください。

```bash
cd projects/APPLaunch
```

リポジトリルートから APPLaunch の `scons` を直接実行しないでください。`PROJECT_PATH`、`SDK_PATH`、`EXT_COMPONENTS_PATH` は現在のプロジェクトディレクトリから導出されます。

### 2.2 Python 依存関係

SCons と Kconfig ツールには Python 3.8 以降が必要です。

```bash
python3 --version
```

一般的な Python パッケージ:

```bash
python3 -m pip install --user parse scons requests tqdm
python3 -m pip install --user setuptools-rust paramiko scp
```

パッケージの用途:

| Package | Purpose |
| --- | --- |
| `scons` | メインのビルド入口 |
| `parse` | SCons スクリプトと SDK ビルドツールが設定/コマンド出力を解析するために使用 |
| `requests`, `tqdm` | SDK ツールが依存ソースコードや sysroot パッケージをダウンロードするときに使用 |
| `paramiko`, `scp` | `scons push` が SSH 経由で `dist` をアップロードするときに使用 |
| `setuptools-rust` | 一部 Python 依存関係のビルド時に必要になる場合がある |

仮想環境を使う場合:

```bash
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install parse scons requests tqdm setuptools-rust paramiko scp
```

## 3. Linux 開発マシンへの依存関係インストール

### 3.1 基本依存関係

Debian/Ubuntu の例:

```bash
sudo apt update
sudo apt install -y \
  python3 python3-pip python3-venv \
  build-essential pkg-config git \
  libffi-dev
```

### 3.2 SDL2 シミュレーション依存関係

Linux SDL2 ビルドは `main/SConstruct` で次を呼びます。

```python
pkg_config_cflags("freetype2")
pkg_config_cflags("sdl2")
pkg_config_ldflags("sdl2")
```

そのため、ホストには SDL2、FreeType、入力関連ライブラリが必要です。

```bash
sudo apt install -y \
  libsdl2-dev libfreetype6-dev \
  libinput-dev libxkbcommon-dev libudev-dev
```

まず `pkg-config` がライブラリを見つけられるか確認することを推奨します。

```bash
pkg-config --cflags sdl2
pkg-config --libs sdl2
pkg-config --cflags freetype2
pkg-config --libs freetype2
```

### 3.3 Linux x86 クロスコンパイル依存関係

Linux x86_64 から M5CardputerZero AArch64 へクロスコンパイルするには、GNU AArch64 クロスツールチェーンが必要です。

```bash
sudo apt install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

確認:

```bash
aarch64-linux-gnu-gcc --version
aarch64-linux-gnu-g++ --version
```

クロスコンパイルにはデバイス側ヘッダーとライブラリも必要です。APPLaunch のトップレベル `SConstruct` は、クロスコンパイル時に SDK 静的 sysroot を自動準備します。

```text
SDK/github_source/static_lib_v0.0.6
```

このディレクトリが存在しない、または `version` ファイルが `v0.0.6` と一致しない場合、ビルドスクリプトは次のリリースパッケージをダウンロードします。

```text
https://github.com/CardputerZero/M5CardputerZero-UserDemo/releases/download/v0.0.6/sdk_bsp.tar.gz
```

そのため、初回クロスコンパイルにはネットワークアクセスが必要です。オフライン環境では、事前に `SDK/github_source/static_lib_v0.0.6` を用意してください。

## 4. macOS への依存関係インストール

### 4.1 Python 環境

仮想環境を推奨します。

```bash
python3 -m venv launcher-python-venv
source launcher-python-venv/bin/activate
pip3 install parse scons requests tqdm setuptools-rust paramiko scp
```

### 4.2 macOS クロスツールチェーン

`mac_cross_cp0_config_defaults.mk` は次を指定します。

```make
CONFIG_TOOLCHAIN_PREFIX="aarch64-unknown-linux-gnu-"
```

インストール:

```bash
brew tap messense/macos-cross-toolchains
brew install aarch64-unknown-linux-gnu
```

確認:

```bash
aarch64-unknown-linux-gnu-gcc --version
aarch64-unknown-linux-gnu-g++ --version
```

### 4.3 macOS SDL/Darwin 依存関係

ネイティブ SDL デバッグに `darwin_config_defaults.mk` を使う場合は、SDL2 と FreeType を用意します。一般的なインストール方法:

```bash
brew install sdl2 freetype pkg-config
```

確認:

```bash
pkg-config --cflags sdl2
pkg-config --cflags freetype2
```

## 5. 主要な環境変数

### 5.1 `CONFIG_DEFAULT_FILE`

`CONFIG_DEFAULT_FILE` は最も重要なビルド選択変数です。

例:

```bash
export CONFIG_DEFAULT_FILE=linux_x86_sdl2_config_defaults.mk
```

SCons はこれを Kconfig に渡し、次を生成します。

```text
build/config/global_config.mk
build/config/global_config.h
```

未設定の場合、`projects/APPLaunch/SConstruct` には自動ロジックがあります。

- `platform.machine()` が `x86_64` のとき、既定で `linux_x86_sdl2_config_defaults.mk` になります。
- 環境変数 `CardputerZero=y` がある場合、`linux_x86_cross_cp0_config_defaults.mk` を強制します。
- デバイス上のネイティブビルドでは、既定ロジックによる誤検出を避けるため、通常 `CONFIG_DEFAULT_FILE=config_defaults.mk` を明示指定する必要があります。

### 5.2 `CardputerZero`

クロスコンパイル設定を選択するショートカットです。

```bash
export CardputerZero=y
```

これはトップレベル `SConstruct` が次を設定するのと同等です。

```text
CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk
```

自動化スクリプトでは、ビルドターゲットのトラブルシュートがしやすいため、引き続き `CONFIG_DEFAULT_FILE` を明示的に書くことを推奨します。

### 5.3 `SDK_PATH` と `EXT_COMPONENTS_PATH`

APPLaunch のトップレベル `SConstruct` は自動的に次を設定します。

```python
os.environ["SDK_PATH"] = str(sdk_path)
os.environ["EXT_COMPONENTS_PATH"] = str(sdk_path.parent / "ext_components")
```

意味:

| Variable | Default value | Purpose |
| --- | --- | --- |
| `SDK_PATH` | リポジトリルート配下の `SDK` | SDK ビルドシステムが Kconfig、SCons ツール、組み込みコンポーネントを見つけるため |
| `EXT_COMPONENTS_PATH` | リポジトリルート配下の `ext_components` | `cp0_lvgl`、`Miniaudio`、`Sigslot`、`RadioLib` などの拡張コンポーネントを読み込むため |

実際に外部 SDK やコンポーネントディレクトリをテストする場合を除き、通常これらの変数を手動で上書きしないでください。

### 5.4 `CONFIG_TOOLCHAIN_SYSROOT`

クロスコンパイル時、トップレベル `SConstruct` は一時設定を自動的に書き込みます。

```text
build/config/config_tmp.mk
```

内容は次のようになります。

```make
CONFIG_TOOLCHAIN_SYSROOT="/path/to/launcher/SDK/github_source/static_lib_v0.0.6"
CONFIG_TOOLCHAIN_FLAGS="-I/path/to/launcher/SDK/github_source/static_lib_v0.0.6/usr/include/aarch64-linux-gnu"
```

これを読み込んだ後、SDK ビルドシステムは次を追加します。

```text
--sysroot=$CONFIG_TOOLCHAIN_SYSROOT
-I$CONFIG_TOOLCHAIN_SYSROOT/usr/include
-I$CONFIG_TOOLCHAIN_SYSROOT/usr/include/<gcc-dumpmachine>
-L$CONFIG_TOOLCHAIN_SYSROOT/lib/<gcc-dumpmachine>
-L$CONFIG_TOOLCHAIN_SYSROOT/usr/lib/<gcc-dumpmachine>
```

`main/SConstruct` もこれを使って FreeType、libpng、libcamera の include/link パスを追加します。

### 5.5 `APPLAUNCH_STARTUP_ANIMATION`

起動アニメーションはオプションのコンパイル時マクロです。

```bash
export APPLAUNCH_STARTUP_ANIMATION=1
```

この変数が `1` の場合、`main/SConstruct` は次を追加します。

```text
-DAPPLAUNCH_STARTUP_ANIMATION
```

未設定の場合、起動アニメーションコードは有効になりません。

### 5.6 ビルド出力のデバッグ

`CONFIG_COMMPILE_DEBUG` が未設定の場合、SDK ビルドシステムは `CXX ...` や `Linking ...` のような簡潔な出力を使います。完全なコンパイラコマンドを見るには次を試してください。

```bash
export CONFIG_COMMPILE_DEBUG=y
scons -j8
```

## 6. Linux SDL2 ネイティブビルドと実行

これは UI 開発で最も一般的なモードです。成果物は Linux x86_64 開発マシン上の SDL2 ウィンドウで実行されます。

### 6.1 古い設定のクリーン

ビルドターゲットを切り替える前にクリーンしてください。特にクロスコンパイルから SDL2 へ戻す場合、古い `build/config/global_config.mk` が前のターゲット設定を保持するため重要です。

```bash
cd /home/nihao/w2T/github/launcher/projects/APPLaunch
scons distclean
```

### 6.2 ビルド

```bash
export CONFIG_DEFAULT_FILE=linux_x86_sdl2_config_defaults.mk
scons -j8
```

現在のマシンが `x86_64` の場合、トップレベル `SConstruct` は SDL2 設定を既定にするため、`CONFIG_DEFAULT_FILE` を省略することもできます。ドキュメントやスクリプトでは明示指定を推奨します。

### 6.3 実行

```bash
cd dist
./M5CardputerZero-APPLaunch
```

SDL2 設定は次を有効にします。

```make
CONFIG_V9_5_LV_USE_SDL=y
CONFIG_V9_5_LV_FS_POSIX_PATH="./"
CONFIG_V9_5_LV_OS_PTHREAD=y
```

そのため、`dist` ディレクトリから実行すると、LVGL の POSIX ファイルシステムルートはカレントディレクトリになり、リソースパスは `./APPLaunch/...` 経由で解決できます。`projects/APPLaunch` から `dist/M5CardputerZero-APPLaunch` を直接実行すると、リソース相対パスが異なる場合があるため、先に `dist` へ入ることを推奨します。

### 6.4 SDL2 ビルドでリンクされるライブラリ

設定ファイルが `linux_x86_sdl2_config_defaults.mk` を含む場合、`main/SConstruct` はさらに次を行います。

- FreeType のコンパイル/リンクパラメータを LVGL コンポーネントへ追加。
- SDL2 のコンパイル/リンクパラメータを APPLaunch へ追加。
- `input`、`xkbcommon`、`udev` をリンク。
- プロジェクト独自のキーボード入力経路との衝突を避けるため、LVGL コンポーネントから `lv_sdl_keyboard.c` を除外。

## 7. デバイス上のネイティブビルド

ネイティブデバイスビルドとは、M5CardputerZero AArch64 Linux システム上で APPLaunch を直接ビルドすることです。利点はツールチェーンと実行時ライブラリが自然にデバイスと一致することです。欠点はデバイスの性能とストレージが限られているため、ビルドが遅いことです。

### 7.1 デバイスへの依存関係インストール

デバイス上で実行:

```bash
sudo apt update
sudo apt install -y \
  python3 python3-pip python3-venv \
  build-essential pkg-config git \
  libffi-dev libfreetype6-dev \
  libinput-dev libxkbcommon-dev libudev-dev \
  libcamera-dev libjpeg-dev
python3 -m pip install --user parse scons requests tqdm setuptools-rust paramiko scp
```

パッケージ名はデバイスイメージによって少し異なる場合があります。`libcamera-dev` が存在しない場合は、まずイメージのパッケージソースが有効か確認するか、システムに既に提供されている libcamera ヘッダーとライブラリを使ってください。

### 7.2 ビルド

```bash
cd /home/pi/launcher/projects/APPLaunch
scons distclean
export CONFIG_DEFAULT_FILE=config_defaults.mk
scons -j2
```

デバイス上では、メモリ不足を避けるため `-j2` または `-j4` を推奨します。`config_defaults.mk` は次を有効にします。

```make
CONFIG_V9_5_LV_USE_LINUX_FBDEV=y
CONFIG_V9_5_LV_USE_EVDEV=y
CONFIG_V9_5_LV_DRAW_SW_ASM_NEON=y
CONFIG_V9_5_LV_USE_DRAW_SW_ASM=1
CONFIG_V9_5_LV_FS_POSIX_PATH="/usr/share/APPLaunch/"
```

### 7.3 実行

デバイス設定のリソースルートパスは `/usr/share/APPLaunch/` なので、`dist` から直接実行すると正式な配置パス配下のリソースを見つけられない場合があります。一時テストには次のいずれかを選びます。

1. リソースを正式な場所へコピーする:

```bash
sudo mkdir -p /usr/share/APPLaunch/bin
sudo cp -a dist/APPLaunch/. /usr/share/APPLaunch/
sudo install -m 0755 dist/M5CardputerZero-APPLaunch /usr/share/APPLaunch/bin/M5CardputerZero-APPLaunch
sudo /usr/share/APPLaunch/bin/M5CardputerZero-APPLaunch
```

2. ホスト側のリソースパスデバッグには SDL2 設定を使います。正式なデバイス実行には使わないでください。

正式なデバイス配備には、第 09 章で説明する `.deb` パッケージングと systemd サービスを使用してください。

## 8. Linux x86 からデバイスへのクロスコンパイル

これは推奨される正式なビルド方法です。Linux x86_64 開発マシンで arm64 成果物を生成し、その後パッケージ化またはデバイスへアップロードします。

### 8.1 クリーンと設定選択

```bash
cd /home/nihao/w2T/github/launcher/projects/APPLaunch
scons distclean
export CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk
scons -j8
```

次も使用できます。

```bash
export CardputerZero=y
scons -j8
```

ただし `CONFIG_DEFAULT_FILE` を明示的に設定することを推奨します。

### 8.2 クロスコンパイル設定の詳細

`linux_x86_cross_cp0_config_defaults.mk` の主要エントリ:

```make
CONFIG_TOOLCHAIN_PREFIX="aarch64-linux-gnu-"
CONFIG_V9_5_LV_USE_LINUX_FBDEV=y
CONFIG_V9_5_LV_LINUX_FBDEV_RENDER_MODE_FULL=y
CONFIG_V9_5_LV_DRAW_SW_ASM_NEON=y
CONFIG_V9_5_LV_USE_DRAW_SW_ASM=1
CONFIG_V9_5_LV_USE_EVDEV=y
CONFIG_V9_5_LV_FS_POSIX_PATH="/usr/share/APPLaunch/"
```

意味:

- `aarch64-linux-gnu-gcc/g++` を使用します。
- デバイス framebuffer を使用し、SDL2 ウィンドウは作成しません。
- evdev でキーボード/入力イベントを読み取ります。
- リソースパスを `/usr/share/APPLaunch/` に固定します。
- NEON アセンブリ最適化を有効にします。
- デバイスの全画面更新戦略に適した full render mode を使います。

### 8.3 自動 sysroot ロジック

トップレベル `SConstruct` は `CONFIG_DEFAULT_FILE` に `cross` が含まれると `cross_package_enabled` を有効にします。

```python
if "cross" in os.environ.get("CONFIG_DEFAULT_FILE", ''):
    cross_package_enabled = True
```

その後、`build/config/config_tmp.mk` に次を生成します。

```text
CONFIG_TOOLCHAIN_SYSROOT="SDK/github_source/static_lib_v0.0.6"
CONFIG_TOOLCHAIN_FLAGS="-I.../usr/include/aarch64-linux-gnu"
```

`SDK/github_source/static_lib_v0.0.6` が存在しない、またはバージョンが一致しない場合、`sdk_bsp.tar.gz` をダウンロードします。この sysroot はクロスコンパイル用に次を提供します。

- デバイス側システムライブラリ。
- FreeType、libpng、libcamera、libjpeg などのヘッダーとライブラリ。
- クロスリンクに必要な `libstdc++.so.6` などの実行時ライブラリ参照。

### 8.4 成果物アーキテクチャの確認

ビルド完了後:

```bash
file dist/M5CardputerZero-APPLaunch
```

期待される出力には次のような文字列が含まれます。

```text
ELF 64-bit LSB executable, ARM aarch64
```

動的依存関係名を確認:

```bash
aarch64-linux-gnu-readelf -d dist/M5CardputerZero-APPLaunch | grep NEEDED
```

開発マシン上でシンボルやセグメント情報を確認する場合:

```bash
aarch64-linux-gnu-readelf -h dist/M5CardputerZero-APPLaunch
aarch64-linux-gnu-objdump -p dist/M5CardputerZero-APPLaunch | grep NEEDED
```

## 9. macOS からデバイスへのクロスコンパイル

### 9.1 ビルドコマンド

```bash
cd /path/to/launcher/projects/APPLaunch
scons distclean
export CONFIG_DEFAULT_FILE=mac_cross_cp0_config_defaults.mk
scons -j8
```

`mac_cross_cp0_config_defaults.mk` は次を使います。

```make
CONFIG_TOOLCHAIN_PREFIX="aarch64-unknown-linux-gnu-"
CONFIG_V9_5_LV_USE_LINUX_FBDEV=y
CONFIG_V9_5_LV_USE_EVDEV=y
CONFIG_V9_5_LV_FS_POSIX_PATH="/usr/share/APPLaunch/"
```

### 9.2 macOS 追加リンクパス

`main/SConstruct` は `mac_cross_cp0_config_defaults.mk` に対して追加処理を行います。

- FreeType と libpng の include を追加: `$CONFIG_TOOLCHAIN_SYSROOT/usr/include/freetype2` と `libpng16`。
- libcamera include を追加。`pkg-config --cflags libcamera` を優先し、失敗時は `$CONFIG_TOOLCHAIN_SYSROOT/usr/include/libcamera` へフォールバック。
- `$CONFIG_TOOLCHAIN_SYSROOT/usr/lib/aarch64-linux-gnu/libstdc++.so.6` をリンク。
- macOS クロスリンカーが sysroot 内の Linux ライブラリを見つけやすいよう、`-Wl,-rpath-link,...` と `-B...` を追加。

### 9.3 macOS の一般的な注意

- Homebrew は Apple Silicon では通常 `/opt/homebrew`、Intel Mac では `/usr/local` 配下です。ツールチェーンが `PATH` にない場合は手動で追加してください。
- `pkg-config` が `libcamera` を見つけられない場合、スクリプトはフォールバックしますが、sysroot には実際のヘッダーとライブラリが含まれている必要があります。
- 生成ファイルは Linux arm64 ELF であり、macOS では直接実行できません。

確認:

```bash
file dist/M5CardputerZero-APPLaunch
```

期待される結果は `ARM aarch64` Linux ELF であり、Mach-O ではありません。

## 10. Windows ビルド

Windows ビルドは `projects/APPLaunch` 配下の同じ SCons 入口を使いますが、設定で `CONFIG_TOOLCHAIN_SYSTEM_WIN=y` と `CONFIG_TOOLCHAIN_GCCSUFFIX=".exe"` を指定し、SDK ビルドシステムが Windows ツールチェーン実行ファイルを呼ぶようにします。

### 10.1 Windows SDL2 ネイティブビルドと実行

MSYS2 MinGW シェルを使用し、`gcc`、`g++`、`pkg-config`、SDL2、FreeType がすべて `PATH` から利用できるようにします。

MSYS2 UCRT64 の例:

```bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-SDL2 \
  mingw-w64-ucrt-x86_64-freetype \
  mingw-w64-ucrt-x86_64-python-pip

python -m pip install parse scons requests tqdm setuptools-rust paramiko scp
```

ビルドと実行:

```bash
cd /path/to/launcher/projects/APPLaunch
scons distclean
export CONFIG_DEFAULT_FILE=win_x86_sdl2_config_defaults.mk
scons -j8
cd dist
./M5CardputerZero-APPLaunch.exe
```

`win_x86_sdl2_config_defaults.mk` の主要エントリ:

```make
CONFIG_TOOLCHAIN_GCCSUFFIX=".exe"
CONFIG_TOOLCHAIN_SYSTEM_WIN=y
CONFIG_V9_5_LV_USE_SDL=y
CONFIG_V9_5_LV_FS_POSIX_PATH="./"
CONFIG_APPLAUNCH_WIN_X86_SDL2=y
```

SDL2 出力は `dist/M5CardputerZero-APPLaunch.exe` です。

### 10.2 Windows からデバイスへのクロスコンパイル

`https://sysprogs.com/getfile/2542/raspberry64-gcc14.2.0.exe` から SysGCC Raspberry64 Windows AArch64 Linux クロスツールチェーンをインストールします。デフォルト設定は次を想定します。

```make
CONFIG_TOOLCHAIN_PATH="D:\\app\\SysGCC\\bin"
CONFIG_TOOLCHAIN_PREFIX="aarch64-linux-gnu-"
CONFIG_TOOLCHAIN_GCCSUFFIX=".exe"
CONFIG_GCC_DUMPMACHINE="aarch64-linux-gnu"
```

ツールチェーンを別の場所へインストールした場合は、ビルド前に `projects/APPLaunch/win_x86_cross_config_defaults.mk` の `CONFIG_TOOLCHAIN_PATH` を更新してください。

ビルド:

```bash
cd /path/to/launcher/projects/APPLaunch
scons distclean
export CONFIG_DEFAULT_FILE=win_x86_cross_config_defaults.mk
scons -j8
```

`win_x86_cross_config_defaults.mk` の主要エントリ:

```make
CONFIG_V9_5_LV_USE_LINUX_FBDEV=y
CONFIG_V9_5_LV_USE_EVDEV=y
CONFIG_V9_5_LV_FS_POSIX_PATH="/usr/share/APPLaunch/"
CONFIG_APPLAUNCH_WIN_X86_CROSS_CP0=y
```

クロスビルド出力は、ターゲットが Linux AArch64 のため `.exe` サフィックスなしの `dist/M5CardputerZero-APPLaunch` です。初回クロスビルドでは、SDK sysroot パッケージが `SDK/github_source/static_lib_v0.0.6` へダウンロードされる場合があります。

## 11. 主要な SCons ロジック

### 11.1 トップレベル `projects/APPLaunch/SConstruct`

このファイルはビルド入口とグローバル環境準備を担当します。

1. SDK パスを定義します。

```text
sdk_path = projects/APPLaunch/../../SDK
```

2. 環境変数に基づいて既定設定を選択します。

```text
CardputerZero=y -> linux_x86_cross_cp0_config_defaults.mk
x86_64 and CONFIG_DEFAULT_FILE unset -> linux_x86_sdl2_config_defaults.mk
```

3. クロスコンパイル時に `build/config/config_tmp.mk` を生成し、sysroot を追加します。

4. 次を設定します。

```text
SDK_PATH
EXT_COMPONENTS_PATH
```

5. SDK ビルドシステムを呼び出します。

```python
SConscript(str(sdk_path / "tools" / "scons" / "project.py"), variant_dir=os.getcwd(), duplicate=0)
```

6. クロスコンパイル時に `static_lib_v0.0.6` を確認し、必要ならダウンロードします。

### 11.2 SDK `project.py`

SDK ビルドシステムは次を行います。

1. 特殊コマンド `menuconfig`、`clean`、`distclean`、`save`、`SET_CROSS`、`push` を処理します。
2. Kconfig ツールを呼び、`global_config.mk` と `global_config.h` を生成します。
3. `global_config.mk` から `CONFIG_...` 変数を環境変数へ読み込みます。
4. SCons ビルド環境とツールチェーンプレフィックスを作成します。
5. SDK コンポーネントディレクトリと `ext_components` ディレクトリをスキャンします。
6. `projects/APPLaunch/main/SConstruct` を読み込み、メインプロジェクトコンポーネントを登録します。
7. 静的ライブラリ、共有ライブラリ、実行ファイルをビルドします。
8. 実行ファイルと `STATIC_FILES` を `dist` へコピーします。

### 11.3 `projects/APPLaunch/main/SConstruct`

このファイルは APPLaunch メインプログラムコンポーネントを登録します。

- `ui/generate_page_app_includes.py` を実行し、組み込みページ include 集約ファイルを生成します。
- 現在の短い git hash を読み取り、コンパイルマクロ `LAUNCHER_GIT_COMMIT_RAW` として注入します。
- `src/*.c*` と `ui` ディレクトリ配下のすべてのソースファイルを収集します。
- include として `main`、`main/include`、`ext_components/cp0_lvgl/include`、`SDK/components/utilities/include` を追加します。
- 依存コンポーネント: `cp0_lvgl`、`eventpp`、`lvgl_component`、`pthread`、`Miniaudio`、`RadioLib`。
- オプション依存: `Backward_cpp`。
- 設定ファイルに応じて SDL2、FreeType、libinput、xkbcommon、udev、libcamera、jpeg などの依存を追加します。Windows SDL2 も Linux SDL2 と同じ SDL2/FreeType `pkg-config` フラグ処理を共有します。
- `ext_components/RadioLib` を静的コンポーネントとして使います。RadioLib コンポーネントは `wget_github('https://github.com/jgromes/RadioLib.git')` のソースキャッシュと SX1262 関連ソースリストを所有します。
- `../APPLaunch` 実行時リソースツリーを `STATIC_FILES` に追加します。
- プロジェクトターゲット `M5CardputerZero-APPLaunch` を登録します。

## 12. よく使う SCons コマンド

| Command | Purpose |
| --- | --- |
| `scons -j8` | 8 並列ジョブでビルド |
| `scons -c` | SCons が把握するターゲットをクリーン |
| `scons distclean` | `build`、`dist`、`.sconsign.dblite`、`.config*` など設定/成果物ファイルを削除 |
| `scons menuconfig` | Kconfig メニューを開き、設定を再生成 |
| `scons save` | 現在の `build/config/global_config.mk` を `CONFIG_DEFAULT_FILE` が指すファイルへ保存 |
| `scons push` | `setup.ini` に従って `dist` を SSH 経由でアップロード |

ターゲット切替時の推奨フロー:

```bash
scons distclean
export CONFIG_DEFAULT_FILE=target-configuration-file
scons -j8
```

`CONFIG_DEFAULT_FILE` だけを変更してすぐ `scons -j8` を実行しないでください。古い `build/config/global_config.mk` が既に存在すると、SDK ビルドシステムは設定を自動再生成しない場合があります。

## 13. `menuconfig` の推奨事項

実行:

```bash
cd projects/APPLaunch
export CONFIG_DEFAULT_FILE=linux_x86_sdl2_config_defaults.mk
scons menuconfig
```

`menuconfig` は `CONFIG_DEFAULT_FILE` と一時設定に基づいて最終設定を生成します。変更後、出力先は次です。

```text
build/config/global_config.mk
build/config/global_config.h
```

変更を永続化したいことが確実な場合:

```bash
scons save
```

注意: `scons save` は設定ファイルへ書き戻します。複数人で作業している場合、このタスクが明示的に要求していない限り、共有の `*_config_defaults.mk` ファイルへ気軽に保存しないでください。

## 14. よくあるエラーと修正

### 14.1 `scons: command not found`

原因: SCons がインストールされていない、または Python user bin ディレクトリが `PATH` にありません。

修正:

```bash
python3 -m pip install --user scons
python3 -m scons --version
```

`python3 -m scons` が動く場合、次の方法でもビルドできます。

```bash
python3 -m scons -j8
```

### 14.2 `ModuleNotFoundError: No module named 'parse'`

原因: Python パッケージ不足。

修正:

```bash
python3 -m pip install --user parse requests tqdm paramiko scp
```

仮想環境では先に `source .venv/bin/activate` を実行します。

### 14.3 `Package sdl2 was not found in the pkg-config search path`

原因: Linux SDL2 シミュレーション依存関係がインストールされていない、または `PKG_CONFIG_PATH` に SDL2 `.pc` ファイルのディレクトリが含まれていません。

修正:

```bash
sudo apt install -y libsdl2-dev pkg-config
pkg-config --cflags sdl2
```

macOS:

```bash
brew install sdl2 pkg-config
pkg-config --cflags sdl2
```

Windows/MSYS2:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-SDL2
pkg-config --cflags sdl2
```

### 14.4 `Package freetype2 was not found`

修正:

```bash
sudo apt install -y libfreetype6-dev
pkg-config --cflags freetype2
```

macOS:

```bash
brew install freetype pkg-config
pkg-config --cflags freetype2
```

Windows/MSYS2:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-freetype
pkg-config --cflags freetype2
```

### 14.5 `aarch64-linux-gnu-gcc: not found`

原因: Linux クロスツールチェーンがインストールされていない、または `PATH` に含まれていません。

修正:

```bash
sudo apt install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
aarch64-linux-gnu-gcc --version
```

macOS クロスコンパイルでは `aarch64-unknown-linux-gnu-gcc` を使います。対応する設定ファイルは `mac_cross_cp0_config_defaults.mk` です。

Windows クロスコンパイルでは `aarch64-linux-gnu-gcc.exe` を使います。`win_x86_cross_config_defaults.mk` の `CONFIG_TOOLCHAIN_PATH` と `CONFIG_TOOLCHAIN_PREFIX` を確認してください。

### 14.6 `sdk_bsp.tar.gz` のダウンロード失敗

原因: 初回クロスコンパイルでは `static_lib_v0.0.6` をダウンロードする必要がありますが、ネットワークがない、または GitHub アクセスに失敗しています。

修正:

1. ネットワークから GitHub release にアクセスできることを確認します。
2. `scons -j8` を再実行します。
3. オフライン環境では次を手動で準備します。

```text
SDK/github_source/static_lib_v0.0.6/
└── version    # content should be v0.0.6
```

ディレクトリが存在してもバージョンが一致しない場合、トップレベル `SConstruct` は更新を試みます。

### 14.7 `libcamera` ヘッダーまたはライブラリが見つからない

クロスコンパイル設定では、`main/SConstruct` が次を追加します。

```text
$CONFIG_TOOLCHAIN_SYSROOT/usr/include/libcamera
-lcamera -lcamera-base -ljpeg
```

修正:

```bash
ls SDK/github_source/static_lib_v0.0.6/usr/include/libcamera
ls SDK/github_source/static_lib_v0.0.6/usr/lib/aarch64-linux-gnu | grep camera
```

不足している場合は sysroot パッケージを更新するか、デバイス側開発ライブラリをインストールして sysroot を再構築してください。

### 14.8 リンクエラー: `cannot find -linput`, `-lxkbcommon`, or `-ludev`

ネイティブ SDL2 ビルド: 開発パッケージをインストールします。

```bash
sudo apt install -y libinput-dev libxkbcommon-dev libudev-dev
```

クロスコンパイル: sysroot を確認します。

```bash
ls SDK/github_source/static_lib_v0.0.6/usr/lib/aarch64-linux-gnu/libinput.*
ls SDK/github_source/static_lib_v0.0.6/usr/lib/aarch64-linux-gnu/libxkbcommon.*
ls SDK/github_source/static_lib_v0.0.6/usr/lib/aarch64-linux-gnu/libudev.*
```

### 14.9 設定切替後も古いバックエンドが使われる

原因: `build/config/global_config.mk` が既に存在し、環境変数を変更しただけではビルドシステムが設定を自動再生成しません。

修正:

```bash
scons distclean
export CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk
scons -j8
```

最終設定を確認:

```bash
grep -E 'LV_USE_SDL|LV_USE_LINUX_FBDEV|LV_USE_EVDEV|FS_POSIX_PATH' build/config/global_config.mk
```

### 14.10 SDL2 実行が黒画面またはリソース欠落になる

よくある原因: プログラムを `dist` ディレクトリから実行しておらず、`CONFIG_V9_5_LV_FS_POSIX_PATH="./"` が間違った場所を指しています。

修正:

```bash
cd projects/APPLaunch/dist
ls APPLaunch/share/images
./M5CardputerZero-APPLaunch
```

### 14.11 デバイスでリソースファイル欠落が報告される

デバイス設定のリソースパス:

```text
/usr/share/APPLaunch/
```

確認:

```bash
ls /usr/share/APPLaunch/share/images
ls /usr/share/APPLaunch/bin/M5CardputerZero-APPLaunch
```

手動配備では、実行ファイルだけでなく `dist/APPLaunch` の内容をコピーしたことを確認してください。

### 14.12 RadioLib ダウンロード失敗

`ext_components/RadioLib/SConstruct` は `CONFIG_RADIOLIB_COMPONENT_ENABLED=y` のとき、`wget_github('https://github.com/jgromes/RadioLib.git')` で RadioLib を取得します。初回ビルドではネットワークアクセスが必要な場合があります。

修正:

- ネットワークから GitHub にアクセスできることを確認します。
- `SDK/github_source` 配下に RadioLib キャッシュが既に存在するか確認します。
- オフライン環境では対応するソースキャッシュを事前に用意します。

## 15. 推奨ビルドフロー

### 15.1 日常的な UI 開発

```bash
cd /home/nihao/w2T/github/launcher/projects/APPLaunch
scons distclean
export CONFIG_DEFAULT_FILE=linux_x86_sdl2_config_defaults.mk
scons -j8
cd dist
./M5CardputerZero-APPLaunch
```

### 15.2 正式なデバイス成果物の生成

```bash
cd /home/nihao/w2T/github/launcher/projects/APPLaunch
scons distclean
export CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk
scons -j8
file dist/M5CardputerZero-APPLaunch
```

その後、第 09 章に従って `.deb` パッケージング、インストール、systemd 検証を行います。

### 15.3 ビルドターゲットを素早く確認

```bash
grep CONFIG_DEFAULT_FILE /proc/$$/environ 2>/dev/null || true
grep -E 'CONFIG_TOOLCHAIN_PREFIX|LV_USE_SDL|LV_USE_LINUX_FBDEV|LV_USE_EVDEV|FS_POSIX_PATH' build/config/global_config.mk
file dist/M5CardputerZero-APPLaunch
```
