# 08 - Build and Compilation Guide

This chapter explains the complete build process for `projects/APPLaunch`, covering Linux SDL2 native simulation, native device builds, Linux x86 cross-compilation, macOS cross-compilation, Windows SDL2/cross builds, dependency installation, environment variables, key SCons logic, and common error handling.

All commands are assumed to start from the repository root by default:

```bash
cd /home/nihao/w2T/github/launcher
```

## 1. Build Target Overview

APPLaunch can be built in several forms. The core difference is determined by the configuration file pointed to by `CONFIG_DEFAULT_FILE`.

| Build target | Run location | Configuration file | Display/input backend | Typical use |
| --- | --- | --- | --- | --- |
| Linux SDL2 native simulation | Linux x86_64 development machine | `linux_x86_sdl2_config_defaults.mk` | SDL2 window + SDL input | Daily UI debugging and rapid development |
| Native device build | M5CardputerZero AArch64 Linux | `config_defaults.mk` | Linux framebuffer + evdev | Build and run directly on the device |
| Linux x86 cross-compilation | Linux x86_64 development machine, output runs on the device | `linux_x86_cross_cp0_config_defaults.mk` | Linux framebuffer + evdev | Recommended way to build official device artifacts |
| macOS cross-compilation | macOS development machine, output runs on the device | `mac_cross_cp0_config_defaults.mk` | Linux framebuffer + evdev | Generate arm64 device artifacts on macOS |
| macOS SDL/Darwin configuration | macOS development machine | `darwin_config_defaults.mk` | SDL-related configuration | Base configuration for native SDL work |
| Windows SDL2 native simulation | Windows x86_64 development machine | `win_x86_sdl2_config_defaults.mk` | SDL2 window + SDL input | UI debugging on Windows |
| Windows x86 cross-compilation | Windows x86_64 development machine, output runs on the device | `win_x86_cross_config_defaults.mk` | Linux framebuffer + evdev | Generate arm64 device artifacts on Windows |

Build artifacts usually appear in:

```text
projects/APPLaunch/dist/
├── M5CardputerZero-APPLaunch
└── APPLaunch/
    └── share/
```

Where:

- `M5CardputerZero-APPLaunch` is the main executable.
- `APPLaunch/` is the runtime resource tree and is copied to `dist/APPLaunch`.

## 2. Prerequisites

### 2.1 Submodules and Directory Layout

For a first clone, use:

```bash
git clone --recursive https://github.com/CardputerZero/launcher.git
cd launcher
```

If the repository was already cloned but submodules were not initialized:

```bash
git submodule update --init --recursive
```

APPLaunch's top-level `SConstruct` assumes this directory relationship:

```text
launcher/
├── SDK/
├── ext_components/
└── projects/
    └── APPLaunch/
        ├── SConstruct
        └── main/SConstruct
```

Enter the APPLaunch project directory before building:

```bash
cd projects/APPLaunch
```

Do not run APPLaunch's `scons` directly from the repository root, because `PROJECT_PATH`, `SDK_PATH`, and `EXT_COMPONENTS_PATH` are derived from the current project directory.

### 2.2 Python Dependencies

SCons and the Kconfig tools require Python 3.8 or later.

```bash
python3 --version
```

Common Python packages:

```bash
python3 -m pip install --user parse scons requests tqdm
python3 -m pip install --user setuptools-rust paramiko scp
```

Package purposes:

| Package | Purpose |
| --- | --- |
| `scons` | Main build entry point |
| `parse` | Used by SCons scripts and SDK build tools to parse configuration/command output |
| `requests`, `tqdm` | Used when SDK tools download dependency source code or sysroot packages |
| `paramiko`, `scp` | Used by `scons push` to upload `dist` over SSH |
| `setuptools-rust` | May be required when building some Python dependencies |

If using a virtual environment:

```bash
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install parse scons requests tqdm setuptools-rust paramiko scp
```

## 3. Installing Dependencies on a Linux Development Machine

### 3.1 Basic Dependencies

Debian/Ubuntu example:

```bash
sudo apt update
sudo apt install -y \
  python3 python3-pip python3-venv \
  build-essential pkg-config git \
  libffi-dev
```

### 3.2 SDL2 Simulation Dependencies

The Linux SDL2 build calls the following in `main/SConstruct`:

```python
pkg_config_cflags("freetype2")
pkg_config_cflags("sdl2")
pkg_config_ldflags("sdl2")
```

Therefore the host needs SDL2, FreeType, and input-related libraries:

```bash
sudo apt install -y \
  libsdl2-dev libfreetype6-dev \
  libinput-dev libxkbcommon-dev libudev-dev
```

It is recommended to first confirm that `pkg-config` can find the libraries:

```bash
pkg-config --cflags sdl2
pkg-config --libs sdl2
pkg-config --cflags freetype2
pkg-config --libs freetype2
```

### 3.3 Linux x86 Cross-Compilation Dependencies

Cross-compiling from Linux x86_64 to M5CardputerZero AArch64 requires the GNU AArch64 cross toolchain:

```bash
sudo apt install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

Verify it:

```bash
aarch64-linux-gnu-gcc --version
aarch64-linux-gnu-g++ --version
```

Cross-compilation also requires device-side headers and libraries. APPLaunch's top-level `SConstruct` automatically prepares the SDK static sysroot during cross-compilation:

```text
SDK/github_source/static_lib_v0.0.6
```

If that directory is missing or its `version` file does not match `v0.0.6`, the build script downloads this release package:

```text
https://github.com/CardputerZero/M5CardputerZero-UserDemo/releases/download/v0.0.6/sdk_bsp.tar.gz
```

Therefore the first cross-compilation needs network access. In offline environments, prepare `SDK/github_source/static_lib_v0.0.6` in advance.

## 4. Installing Dependencies on macOS

### 4.1 Python Environment

A virtual environment is recommended:

```bash
python3 -m venv launcher-python-venv
source launcher-python-venv/bin/activate
pip3 install parse scons requests tqdm setuptools-rust paramiko scp
```

### 4.2 macOS Cross Toolchain

`mac_cross_cp0_config_defaults.mk` specifies:

```make
CONFIG_TOOLCHAIN_PREFIX="aarch64-unknown-linux-gnu-"
```

Install it with:

```bash
brew tap messense/macos-cross-toolchains
brew install aarch64-unknown-linux-gnu
```

Verify it:

```bash
aarch64-unknown-linux-gnu-gcc --version
aarch64-unknown-linux-gnu-g++ --version
```

### 4.3 macOS SDL/Darwin Dependencies

If using `darwin_config_defaults.mk` for native SDL debugging, prepare SDL2 and FreeType. A common installation method is:

```bash
brew install sdl2 freetype pkg-config
```

Confirm:

```bash
pkg-config --cflags sdl2
pkg-config --cflags freetype2
```

## 5. Key Environment Variables

### 5.1 `CONFIG_DEFAULT_FILE`

`CONFIG_DEFAULT_FILE` is the most important build selection variable.

Example:

```bash
export CONFIG_DEFAULT_FILE=linux_x86_sdl2_config_defaults.mk
```

SCons passes it to Kconfig to generate:

```text
build/config/global_config.mk
build/config/global_config.h
```

If it is not set, `projects/APPLaunch/SConstruct` has automatic logic:

- When `platform.machine()` is `x86_64`, it defaults to `linux_x86_sdl2_config_defaults.mk`.
- If environment variable `CardputerZero=y`, it forces `linux_x86_cross_cp0_config_defaults.mk`.
- Native device builds usually need `CONFIG_DEFAULT_FILE=config_defaults.mk` specified explicitly to avoid incorrect detection by the default logic.

### 5.2 `CardputerZero`

Shortcut for selecting the cross-compilation configuration:

```bash
export CardputerZero=y
```

This is equivalent to having the top-level `SConstruct` set:

```text
CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk
```

In automation scripts, it is still recommended to write `CONFIG_DEFAULT_FILE` explicitly because it makes build-target troubleshooting easier.

### 5.3 `SDK_PATH` and `EXT_COMPONENTS_PATH`

APPLaunch's top-level `SConstruct` automatically sets:

```python
os.environ["SDK_PATH"] = str(sdk_path)
os.environ["EXT_COMPONENTS_PATH"] = str(sdk_path.parent / "ext_components")
```

Meaning:

| Variable | Default value | Purpose |
| --- | --- | --- |
| `SDK_PATH` | `SDK` under the repository root | Lets the SDK build system find Kconfig, SCons tools, and built-in components |
| `EXT_COMPONENTS_PATH` | `ext_components` under the repository root | Lets the build system load extension components such as `cp0_lvgl`, `Miniaudio`, `Sigslot`, and `RadioLib` |

Usually do not override these variables manually unless you are actually testing an external SDK or component directory.

### 5.4 `CONFIG_TOOLCHAIN_SYSROOT`

During cross-compilation, the top-level `SConstruct` automatically writes a temporary configuration:

```text
build/config/config_tmp.mk
```

The content looks like:

```make
CONFIG_TOOLCHAIN_SYSROOT="/path/to/launcher/SDK/github_source/static_lib_v0.0.6"
CONFIG_TOOLCHAIN_FLAGS="-I/path/to/launcher/SDK/github_source/static_lib_v0.0.6/usr/include/aarch64-linux-gnu"
```

After reading it, the SDK build system appends:

```text
--sysroot=$CONFIG_TOOLCHAIN_SYSROOT
-I$CONFIG_TOOLCHAIN_SYSROOT/usr/include
-I$CONFIG_TOOLCHAIN_SYSROOT/usr/include/<gcc-dumpmachine>
-L$CONFIG_TOOLCHAIN_SYSROOT/lib/<gcc-dumpmachine>
-L$CONFIG_TOOLCHAIN_SYSROOT/usr/lib/<gcc-dumpmachine>
```

`main/SConstruct` also uses it to append include and link paths for FreeType, libpng, and libcamera.

### 5.5 `APPLAUNCH_STARTUP_ANIMATION`

The startup animation is an optional compile-time macro:

```bash
export APPLAUNCH_STARTUP_ANIMATION=1
```

When this variable is `1`, `main/SConstruct` adds:

```text
-DAPPLAUNCH_STARTUP_ANIMATION
```

If it is not set, startup-animation code is not enabled.

### 5.6 Debugging Build Output

When `CONFIG_COMMPILE_DEBUG` is not set, the SDK build system uses concise output such as `CXX ...` and `Linking ...`. To see full compiler commands, try:

```bash
export CONFIG_COMMPILE_DEBUG=y
scons -j8
```

## 6. Linux SDL2 Native Build and Run

This is the most common mode for UI development. The artifact runs in an SDL2 window on a Linux x86_64 development machine.

### 6.1 Clean Old Configuration

Clean before switching build targets. This is especially important when switching from cross-compilation back to SDL2 because the old `build/config/global_config.mk` keeps the previous target configuration.

```bash
cd /home/nihao/w2T/github/launcher/projects/APPLaunch
scons distclean
```

### 6.2 Build

```bash
export CONFIG_DEFAULT_FILE=linux_x86_sdl2_config_defaults.mk
scons -j8
```

If the current machine is `x86_64`, you can also omit `CONFIG_DEFAULT_FILE`, because the top-level `SConstruct` defaults to the SDL2 configuration. In documentation and scripts, explicit selection is recommended.

### 6.3 Run

```bash
cd dist
./M5CardputerZero-APPLaunch
```

The SDL2 configuration enables:

```make
CONFIG_V9_5_LV_USE_SDL=y
CONFIG_V9_5_LV_FS_POSIX_PATH="./"
CONFIG_V9_5_LV_OS_PTHREAD=y
```

Therefore, when running from the `dist` directory, LVGL's POSIX filesystem root is the current directory and resource paths can resolve through `./APPLaunch/...`. If you run `dist/M5CardputerZero-APPLaunch` directly from `projects/APPLaunch`, resource-relative paths may differ, so entering `dist` first is recommended.

### 6.4 Libraries Linked by the SDL2 Build

When the configuration file includes `linux_x86_sdl2_config_defaults.mk`, `main/SConstruct` additionally:

- Adds FreeType compile and link parameters to the LVGL component.
- Adds SDL2 compile and link parameters to APPLaunch.
- Links `input`, `xkbcommon`, and `udev`.
- Filters out `lv_sdl_keyboard.c` from the LVGL component to avoid conflicts with the project's custom keyboard input path.

## 7. Native Device Build

A native device build means building APPLaunch directly on the M5CardputerZero AArch64 Linux system. The advantage is that the toolchain and runtime libraries naturally match the device; the disadvantage is that device performance and storage are limited, so builds are slower.

### 7.1 Install Dependencies on the Device

Run on the device:

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

Package names may differ slightly between device images. If `libcamera-dev` does not exist, first confirm whether the image's package sources are enabled, or use the libcamera headers and libraries already provided by the system.

### 7.2 Build

```bash
cd /home/pi/launcher/projects/APPLaunch
scons distclean
export CONFIG_DEFAULT_FILE=config_defaults.mk
scons -j2
```

On the device, `-j2` or `-j4` is recommended to avoid running out of memory. `config_defaults.mk` enables:

```make
CONFIG_V9_5_LV_USE_LINUX_FBDEV=y
CONFIG_V9_5_LV_USE_EVDEV=y
CONFIG_V9_5_LV_DRAW_SW_ASM_NEON=y
CONFIG_V9_5_LV_USE_DRAW_SW_ASM=1
CONFIG_V9_5_LV_FS_POSIX_PATH="/usr/share/APPLaunch/"
```

### 7.3 Run

The resource root path for the device configuration is `/usr/share/APPLaunch/`, so running directly from `dist` may fail to find resources under the formal deployment path. For temporary testing, choose one of the following:

1. Copy resources to the formal location:

```bash
sudo mkdir -p /usr/share/APPLaunch/bin
sudo cp -a dist/APPLaunch/. /usr/share/APPLaunch/
sudo install -m 0755 dist/M5CardputerZero-APPLaunch /usr/share/APPLaunch/bin/M5CardputerZero-APPLaunch
sudo /usr/share/APPLaunch/bin/M5CardputerZero-APPLaunch
```

2. Use the SDL2 configuration for host-side resource-path debugging; do not use it for formal device execution.

Formal device deployment should use the `.deb` packaging and systemd service described in Chapter 09.

## 8. Linux x86 Cross-Compilation to the Device

This is the recommended formal build method: generate arm64 artifacts on a Linux x86_64 development machine, then package or upload them to the device.

### 8.1 Clean and Select Configuration

```bash
cd /home/nihao/w2T/github/launcher/projects/APPLaunch
scons distclean
export CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk
scons -j8
```

You can also use:

```bash
export CardputerZero=y
scons -j8
```

But setting `CONFIG_DEFAULT_FILE` explicitly is recommended.

### 8.2 Cross-Compilation Configuration Details

Key entries in `linux_x86_cross_cp0_config_defaults.mk`:

```make
CONFIG_TOOLCHAIN_PREFIX="aarch64-linux-gnu-"
CONFIG_V9_5_LV_USE_LINUX_FBDEV=y
CONFIG_V9_5_LV_LINUX_FBDEV_RENDER_MODE_FULL=y
CONFIG_V9_5_LV_DRAW_SW_ASM_NEON=y
CONFIG_V9_5_LV_USE_DRAW_SW_ASM=1
CONFIG_V9_5_LV_USE_EVDEV=y
CONFIG_V9_5_LV_FS_POSIX_PATH="/usr/share/APPLaunch/"
```

Meaning:

- Use `aarch64-linux-gnu-gcc/g++`.
- Use the device framebuffer; no SDL2 window is created.
- Use evdev to read keyboard/input events.
- Fix the resource path to `/usr/share/APPLaunch/`.
- Enable NEON assembly optimization.
- Use full render mode, which suits the device's full-screen refresh strategy.

### 8.3 Automatic Sysroot Logic

When the top-level `SConstruct` sees `cross` in `CONFIG_DEFAULT_FILE`, it enables `cross_package_enabled`:

```python
if "cross" in os.environ.get("CONFIG_DEFAULT_FILE", ''):
    cross_package_enabled = True
```

It then generates `build/config/config_tmp.mk` with:

```text
CONFIG_TOOLCHAIN_SYSROOT="SDK/github_source/static_lib_v0.0.6"
CONFIG_TOOLCHAIN_FLAGS="-I.../usr/include/aarch64-linux-gnu"
```

If `SDK/github_source/static_lib_v0.0.6` is missing or its version does not match, it downloads `sdk_bsp.tar.gz`. This sysroot provides cross-compilation with:

- Device-side system libraries.
- Headers and libraries for FreeType, libpng, libcamera, libjpeg, and others.
- Runtime-library references such as `libstdc++.so.6` needed for cross-linking.

### 8.4 Verify Artifact Architecture

After the build completes:

```bash
file dist/M5CardputerZero-APPLaunch
```

Expected output should contain something like:

```text
ELF 64-bit LSB executable, ARM aarch64
```

Check dynamic dependency names:

```bash
aarch64-linux-gnu-readelf -d dist/M5CardputerZero-APPLaunch | grep NEEDED
```

If you need to inspect symbols or segment information on the development machine:

```bash
aarch64-linux-gnu-readelf -h dist/M5CardputerZero-APPLaunch
aarch64-linux-gnu-objdump -p dist/M5CardputerZero-APPLaunch | grep NEEDED
```

## 9. macOS Cross-Compilation to the Device

### 9.1 Build Command

```bash
cd /path/to/launcher/projects/APPLaunch
scons distclean
export CONFIG_DEFAULT_FILE=mac_cross_cp0_config_defaults.mk
scons -j8
```

`mac_cross_cp0_config_defaults.mk` uses:

```make
CONFIG_TOOLCHAIN_PREFIX="aarch64-unknown-linux-gnu-"
CONFIG_V9_5_LV_USE_LINUX_FBDEV=y
CONFIG_V9_5_LV_USE_EVDEV=y
CONFIG_V9_5_LV_FS_POSIX_PATH="/usr/share/APPLaunch/"
```

### 9.2 Additional macOS Link Paths

`main/SConstruct` performs extra handling for `mac_cross_cp0_config_defaults.mk`:

- Adds FreeType and libpng includes: `$CONFIG_TOOLCHAIN_SYSROOT/usr/include/freetype2` and `libpng16`.
- Adds libcamera includes, preferring `pkg-config --cflags libcamera` and falling back to `$CONFIG_TOOLCHAIN_SYSROOT/usr/include/libcamera` if that fails.
- Links `$CONFIG_TOOLCHAIN_SYSROOT/usr/lib/aarch64-linux-gnu/libstdc++.so.6`.
- Appends `-Wl,-rpath-link,...` and `-B...` to help the macOS cross linker find Linux libraries inside the sysroot.

### 9.3 Common macOS Notes

- Homebrew is usually under `/opt/homebrew` on Apple Silicon and `/usr/local` on Intel Mac. If the toolchain is not in `PATH`, add it manually.
- If `pkg-config` cannot find `libcamera`, the script falls back, but the sysroot must still contain the actual headers and libraries.
- The generated file is a Linux arm64 ELF and cannot run directly on macOS.

Verify:

```bash
file dist/M5CardputerZero-APPLaunch
```

The expected result is an `ARM aarch64` Linux ELF, not Mach-O.

## 10. Windows Builds

Windows builds use the same SCons entry point under `projects/APPLaunch`, but the configuration sets `CONFIG_TOOLCHAIN_SYSTEM_WIN=y` and `CONFIG_TOOLCHAIN_GCCSUFFIX=".exe"` so the SDK build system invokes Windows toolchain executables.

### 10.1 Windows SDL2 Native Build and Run

Use an MSYS2 MinGW shell so `gcc`, `g++`, `pkg-config`, SDL2, and FreeType are all available in `PATH`.

MSYS2 UCRT64 example:

```bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-SDL2 \
  mingw-w64-ucrt-x86_64-freetype \
  mingw-w64-ucrt-x86_64-python-pip

python -m pip install parse scons requests tqdm setuptools-rust paramiko scp
```

Build and run:

```bash
cd /path/to/launcher/projects/APPLaunch
scons distclean
export CONFIG_DEFAULT_FILE=win_x86_sdl2_config_defaults.mk
scons -j8
cd dist
./M5CardputerZero-APPLaunch.exe
```

Key entries in `win_x86_sdl2_config_defaults.mk`:

```make
CONFIG_TOOLCHAIN_GCCSUFFIX=".exe"
CONFIG_TOOLCHAIN_SYSTEM_WIN=y
CONFIG_V9_5_LV_USE_SDL=y
CONFIG_V9_5_LV_FS_POSIX_PATH="./"
CONFIG_APPLAUNCH_WIN_X86_SDL2=y
```

The SDL2 output is `dist/M5CardputerZero-APPLaunch.exe`.

### 10.2 Windows Cross-Compilation to the Device

Install the SysGCC Raspberry64 Windows AArch64 Linux cross toolchain from `https://sysprogs.com/getfile/2542/raspberry64-gcc14.2.0.exe`. The default configuration expects:

```make
CONFIG_TOOLCHAIN_PATH="D:\\app\\SysGCC\\bin"
CONFIG_TOOLCHAIN_PREFIX="aarch64-linux-gnu-"
CONFIG_TOOLCHAIN_GCCSUFFIX=".exe"
CONFIG_GCC_DUMPMACHINE="aarch64-linux-gnu"
```

If the toolchain is installed elsewhere, update `CONFIG_TOOLCHAIN_PATH` in `projects/APPLaunch/win_x86_cross_config_defaults.mk` before building.

Build:

```bash
cd /path/to/launcher/projects/APPLaunch
scons distclean
export CONFIG_DEFAULT_FILE=win_x86_cross_config_defaults.mk
scons -j8
```

Key entries in `win_x86_cross_config_defaults.mk`:

```make
CONFIG_V9_5_LV_USE_LINUX_FBDEV=y
CONFIG_V9_5_LV_USE_EVDEV=y
CONFIG_V9_5_LV_FS_POSIX_PATH="/usr/share/APPLaunch/"
CONFIG_APPLAUNCH_WIN_X86_CROSS_CP0=y
```

The cross-build output is `dist/M5CardputerZero-APPLaunch` with no `.exe` suffix because the target is Linux AArch64. The first cross-build may download the SDK sysroot package into `SDK/github_source/static_lib_v0.0.6`.

## 11. Key SCons Logic

### 11.1 Top-Level `projects/APPLaunch/SConstruct`

This file is responsible for the build entry point and global environment preparation:

1. Defines the SDK path:

```text
sdk_path = projects/APPLaunch/../../SDK
```

2. Selects the default configuration based on environment variables:

```text
CardputerZero=y -> linux_x86_cross_cp0_config_defaults.mk
x86_64 and CONFIG_DEFAULT_FILE unset -> linux_x86_sdl2_config_defaults.mk
```

3. Generates `build/config/config_tmp.mk` during cross-compilation to add the sysroot.

4. Sets:

```text
SDK_PATH
EXT_COMPONENTS_PATH
```

5. Calls the SDK build system:

```python
SConscript(str(sdk_path / "tools" / "scons" / "project.py"), variant_dir=os.getcwd(), duplicate=0)
```

6. Checks and downloads `static_lib_v0.0.6` during cross-compilation.

### 11.2 SDK `project.py`

The SDK build system does the following:

1. Handles special commands: `menuconfig`, `clean`, `distclean`, `save`, `SET_CROSS`, and `push`.
2. Calls the Kconfig tool to generate `global_config.mk` and `global_config.h`.
3. Loads `CONFIG_...` variables from `global_config.mk` into environment variables.
4. Creates the SCons build environment and toolchain prefix.
5. Scans SDK component directories and the `ext_components` directory.
6. Loads `projects/APPLaunch/main/SConstruct` to register the main project component.
7. Builds static libraries, shared libraries, and executables.
8. Copies the executable and `STATIC_FILES` to `dist`.

### 11.3 `projects/APPLaunch/main/SConstruct`

This file registers the APPLaunch main-program component:

- Runs `ui/generate_page_app_includes.py` to generate the built-in page include aggregation file.
- Reads the current short git hash and injects compile macro `LAUNCHER_GIT_COMMIT_RAW`.
- Collects `src/*.c*` and all source files under the `ui` directory.
- Adds includes: `main`, `main/include`, `ext_components/cp0_lvgl/include`, and `SDK/components/utilities/include`.
- Depends on components: `cp0_lvgl`, `eventpp`, `lvgl_component`, `pthread`, `Miniaudio`, and `RadioLib`.
- Optional dependency: `Backward_cpp`.
- Adds SDL2, FreeType, libinput, xkbcommon, udev, libcamera, jpeg, and other dependencies according to different configuration files; Windows SDL2 shares the same SDL2/FreeType `pkg-config` flag handling as Linux SDL2.
- Uses `ext_components/RadioLib` as a static component; the RadioLib component owns the `wget_github('https://github.com/jgromes/RadioLib.git')` source cache and SX1262-related source list.
- Adds the `../APPLaunch` runtime resource tree to `STATIC_FILES`.
- Registers project target: `M5CardputerZero-APPLaunch`.

## 12. Common SCons Commands

| Command | Purpose |
| --- | --- |
| `scons -j8` | Build with 8 parallel jobs |
| `scons -c` | Clean targets known to SCons |
| `scons distclean` | Delete `build`, `dist`, `.sconsign.dblite`, `.config*`, and other configuration/artifact files |
| `scons menuconfig` | Open the Kconfig menu and regenerate configuration |
| `scons save` | Save the current `build/config/global_config.mk` back to the file pointed to by `CONFIG_DEFAULT_FILE` |
| `scons push` | Upload `dist` over SSH according to `setup.ini` |

Recommended flow when switching targets:

```bash
scons distclean
export CONFIG_DEFAULT_FILE=target-configuration-file
scons -j8
```

Do not simply change `CONFIG_DEFAULT_FILE` and immediately run `scons -j8`, because the old `build/config/global_config.mk` may already exist and the SDK build system will not automatically regenerate the configuration.

## 13. `menuconfig` Recommendations

Run:

```bash
cd projects/APPLaunch
export CONFIG_DEFAULT_FILE=linux_x86_sdl2_config_defaults.mk
scons menuconfig
```

`menuconfig` generates the final configuration based on `CONFIG_DEFAULT_FILE` and the temporary configuration. After modification, it outputs to:

```text
build/config/global_config.mk
build/config/global_config.h
```

If you are sure you want to make the changes persistent:

```bash
scons save
```

Note: `scons save` writes back to the configuration file. In multi-person collaboration, do not casually save to shared `*_config_defaults.mk` files unless this task explicitly requires that change.

## 14. Common Errors and Fixes

### 14.1 `scons: command not found`

Cause: SCons is not installed, or the Python user bin directory is not in `PATH`.

Fix:

```bash
python3 -m pip install --user scons
python3 -m scons --version
```

If `python3 -m scons` works, you can also build this way:

```bash
python3 -m scons -j8
```

### 14.2 `ModuleNotFoundError: No module named 'parse'`

Cause: missing Python package.

Fix:

```bash
python3 -m pip install --user parse requests tqdm paramiko scp
```

In a virtual environment, run `source .venv/bin/activate` first.

### 14.3 `Package sdl2 was not found in the pkg-config search path`

Cause: Linux SDL2 simulation dependencies are not installed, or `PKG_CONFIG_PATH` does not include the directory containing SDL2 `.pc` files.

Fix:

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

Fix:

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

Cause: Linux cross toolchain is not installed, or `PATH` does not include the toolchain.

Fix:

```bash
sudo apt install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
aarch64-linux-gnu-gcc --version
```

macOS cross-compilation should use `aarch64-unknown-linux-gnu-gcc`; the corresponding configuration file is `mac_cross_cp0_config_defaults.mk`.

Windows cross-compilation should use `aarch64-linux-gnu-gcc.exe`; check `CONFIG_TOOLCHAIN_PATH` and `CONFIG_TOOLCHAIN_PREFIX` in `win_x86_cross_config_defaults.mk`.

### 14.6 Failed to Download `sdk_bsp.tar.gz`

Cause: the first cross-compilation needs to download `static_lib_v0.0.6`, but the network is unavailable or GitHub access failed.

Fix:

1. Confirm that the network can access the GitHub release.
2. Run `scons -j8` again.
3. For offline environments, prepare this manually:

```text
SDK/github_source/static_lib_v0.0.6/
└── version    # content should be v0.0.6
```

If the directory exists but the version does not match, the top-level `SConstruct` still tries to update it.

### 14.7 `libcamera` Headers or Libraries Not Found

In cross-compilation configurations, `main/SConstruct` adds:

```text
$CONFIG_TOOLCHAIN_SYSROOT/usr/include/libcamera
-lcamera -lcamera-base -ljpeg
```

Fix:

```bash
ls SDK/github_source/static_lib_v0.0.6/usr/include/libcamera
ls SDK/github_source/static_lib_v0.0.6/usr/lib/aarch64-linux-gnu | grep camera
```

If they are missing, update the sysroot package or install device-side development libraries and rebuild the sysroot.

### 14.8 Link Errors: `cannot find -linput`, `-lxkbcommon`, or `-ludev`

Native SDL2 build: install development packages.

```bash
sudo apt install -y libinput-dev libxkbcommon-dev libudev-dev
```

Cross-compilation: check the sysroot:

```bash
ls SDK/github_source/static_lib_v0.0.6/usr/lib/aarch64-linux-gnu/libinput.*
ls SDK/github_source/static_lib_v0.0.6/usr/lib/aarch64-linux-gnu/libxkbcommon.*
ls SDK/github_source/static_lib_v0.0.6/usr/lib/aarch64-linux-gnu/libudev.*
```

### 14.9 Old Backend Still Used After Switching Configuration

Cause: `build/config/global_config.mk` already exists, and the build system will not automatically regenerate the configuration just because the environment variable changed.

Fix:

```bash
scons distclean
export CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk
scons -j8
```

Check the final configuration:

```bash
grep -E 'LV_USE_SDL|LV_USE_LINUX_FBDEV|LV_USE_EVDEV|FS_POSIX_PATH' build/config/global_config.mk
```

### 14.10 SDL2 Runs to a Black Screen or Missing Resources

Common cause: the program was not run from the `dist` directory, so `CONFIG_V9_5_LV_FS_POSIX_PATH="./"` points to the wrong location.

Fix:

```bash
cd projects/APPLaunch/dist
ls APPLaunch/share/images
./M5CardputerZero-APPLaunch
```

### 14.11 Device Reports Missing Resource Files

The device configuration resource path is:

```text
/usr/share/APPLaunch/
```

Check:

```bash
ls /usr/share/APPLaunch/share/images
ls /usr/share/APPLaunch/bin/M5CardputerZero-APPLaunch
```

For manual deployment, make sure you copied the contents of `dist/APPLaunch`, not only the executable.

### 14.12 RadioLib Download Failure

`ext_components/RadioLib/SConstruct` uses `wget_github('https://github.com/jgromes/RadioLib.git')` to fetch RadioLib when `CONFIG_RADIOLIB_COMPONENT_ENABLED=y`. The first build may need network access.

Fix:

- Confirm that the network can access GitHub.
- Check whether a RadioLib cache already exists under `SDK/github_source`.
- Prepare the corresponding source cache in advance for offline environments.

## 15. Recommended Build Flows

### 15.1 Daily UI Development

```bash
cd /home/nihao/w2T/github/launcher/projects/APPLaunch
scons distclean
export CONFIG_DEFAULT_FILE=linux_x86_sdl2_config_defaults.mk
scons -j8
cd dist
./M5CardputerZero-APPLaunch
```

### 15.2 Generate Formal Device Artifacts

```bash
cd /home/nihao/w2T/github/launcher/projects/APPLaunch
scons distclean
export CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk
scons -j8
file dist/M5CardputerZero-APPLaunch
```

Then follow Chapter 09 for `.deb` packaging, installation, and systemd verification.

### 15.3 Quickly Confirm the Build Target

```bash
grep CONFIG_DEFAULT_FILE /proc/$$/environ 2>/dev/null || true
grep -E 'CONFIG_TOOLCHAIN_PREFIX|LV_USE_SDL|LV_USE_LINUX_FBDEV|LV_USE_EVDEV|FS_POSIX_PATH' build/config/global_config.mk
file dist/M5CardputerZero-APPLaunch
```
