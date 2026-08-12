# launcher

[English](./README.md) | [日本語](./README_JA.md)

基于 [M5Stack_Linux_Libs](https://github.com/m5stack/M5Stack_Linux_Libs) SDK 开发的 M5CardputerZero 应用集合。该项目展示了如何在 M5CardputerZero（AArch64 Linux）设备上使用 **LVGL 9.5** 构建图形界面应用。

仓库包含三个主要项目，并提供若干示例/辅助项目：
- **HelloWorld** — 基础用户演示程序，展示状态栏和简单 UI
- **APPLaunch** — 应用启动器，提供多应用导航、LoRa 通信、音频播放等丰富功能
- **ZClaw** — 面向键盘操作的 LVGL ZeroClaw Webhook 聊天客户端，支持引导式模型服务配置与工具审批

UI 界面由 **SquareLine Studio 1.5.0** 生成，支持 SDL2 仿真模式（本机调试）和 Linux Framebuffer 模式（设备运行）两种显示后端。

---

## 项目结构

```
launcher/
├── SDK/                        # M5Stack_Linux_Libs SDK（git submodule）
│   ├── components/             # 组件库（lvgl_component、DeviceDriver 等）
│   ├── examples/               # SDK 示例程序
│   └── tools/                  # 编译工具链脚本（SCons）
├── projects/
│   ├── UserDemo/               # 基础用户演示项目
│   ├── APPLaunch/              # 应用启动器项目（核心）
│   │   ├── SConstruct          # 项目顶层编译脚本
│   │   ├── config_defaults.mk  # 默认编译配置（Linux Framebuffer 模式）
│   │   ├── darwin_config_defaults.mk  # macOS 编译配置
│   │   ├── win_x86_sdl2_config_defaults.mk  # Windows SDL2 仿真配置
│   │   ├── win_x86_cross_config_defaults.mk # Windows 到设备交叉编译配置
│   │   ├── setup.ini           # SSH 部署配置
│   │   └── main/               # 主程序源码
│   │       ├── SConstruct      # 组件编译脚本
│   │       ├── src/
│   │       │   └── main.cpp    # 程序入口
│   │       └── ui/             # UI 代码
│   │           ├── ui.h / ui.cpp          # UI 初始化
│   │           ├── launch.cpp / launch.h  # 页面/进程启动生命周期
│   │           ├── app_registry.*         # 内置应用开关注册表
│   │           ├── builtin_app_registry.* # 内置入口定义
│   │           ├── desktop_app_loader.*   # 动态 .desktop 入口加载
│   │           ├── app_directory_watcher.* # 动态应用目录监听
│   │           ├── ui_launch_page.*       # 首页轮播 UI
│   │           ├── launcher_ui_runtime.*  # LVGL 运行时/首页引导
│   │           ├── animation/             # 动画效果
│   │           ├── model/                 # 可测试的 UI 状态与策略
│   │           └── page_app/              # 内置页面及视图实现
│   ├── ZClaw/                  # ZeroClaw Webhook 聊天客户端
│   │   ├── SConstruct          # 项目编译脚本
│   │   ├── *_config_defaults.mk # 设备、SDL2 与交叉编译配置
│   │   └── main/ui/            # 聊天 UI、客户端、初始化与运行时字体
│   ├── LaunchWizard/           # 首次启动向导（标准 CMake/CTest + SDK SCons）
│   ├── Calculator/             # 计算器
│   ├── AppStore/               # 应用商店
│   └── HelloWorld/             # Hello World 示例
├── ext_components/             # 外部组件（Miniaudio、RadioLib 等）
├── docs/                       # 工程文档
├── scripts/                    # 仓库辅助工具
├── README.md
├── README_ZH.md
└── README_JA.md
```

---

## 功能特性

### 通用特性

- 基于 LVGL 9.5 的图形界面
- 支持两种显示后端：
  - **SDL2**：用于 PC 端仿真调试（默认编译模式）
  - **Linux Framebuffer（ST7789V）**：用于 M5CardputerZero 设备端运行
- evdev 键盘/触摸输入支持（设备端）
- 使用 SCons + Kconfig 构建系统，可通过 `scons menuconfig` 灵活配置

### APPLaunch 特性

- 应用启动器界面，支持多应用导航（轮播翻页）
- 所有构建均注册 Python、Store、CLI/ST 终端、Game、Setting、Math 和 LoRa；Linux 设备构建另外注册 IP Panel、SSH 和 Tank Battle
- LoRa 通信（基于 RadioLib SX1262）
- 音频播放（基于 Miniaudio）
- 电池状态监控与显示
- 全局快捷键提示（ESC / Shift / SYM）
- 应用进程锁定管理（长按 Home 键强制关闭应用）
- 多线程任务池（C-Thread-Pool）
- 支持 macOS 交叉编译

### ZClaw 特性

- 针对 320×170 屏幕设计的键盘优先聊天界面
- 首次启动引导配置 OpenAI、OpenRouter、Anthropic、Ollama、DeepSeek 与自定义服务
- 支持 ZeroClaw Webhook 配对、Bearer/Secret 认证及本地持久化配置
- 支持 Agent 返回工具请求时的交互式审批
- 支持 FreeType 运行时字体及 `ZCLAW_FONT`、`ZCLAW_FALLBACK_FONT` 环境变量覆盖
- 提供设备端、Linux SDL2 仿真、Linux 交叉编译和 macOS 交叉编译配置

### UserDemo 特性

- 状态栏显示：M5Stack Zero Logo、时钟时间、电量百分比
- 主内容区：应用名称 + 页面内容占位

---

## 编译程序

如需启用按键注入、framebuffer 截图等本地测试自动化，请参阅 [APPLaunch 本地自动化 RPC](docs/automation-rpc.md)。

项目主要在 Linux 环境下开发，同时兼容跨平台构建。下面是详细编译说明。

### Linux
#### 依赖安装（仅需执行一次）

```bash
sudo apt update
sudo apt install python3 python3-pip build-essential pkg-config libffi-dev libsdl2-dev libfreetype6-dev

pip3 install parse scons requests tqdm
pip3 install setuptools-rust paramiko scp
```

> Python 版本需 ≥ 3.8

#### 克隆项目（含子模块）

```bash
git clone --recursive https://github.com/CardputerZero/launcher.git
cd launcher
```

如果已克隆但未初始化子模块：

```bash
git submodule update --init --recursive
```
进入 `HelloWorld` 进行编译：

#### 设备上编译
```bash
# 进入工程目录
cd projects/HelloWorld
# 清理环境
scons distclean
# 启动 8 线程编译
scons -j8
```

#### 编译 SDL2 模拟器
```bash
# 进入工程目录
cd projects/HelloWorld
# 加载配置项
export CONFIG_DEFAULT_FILE=linux_x86_sdl2_config_defaults.mk
# 清理环境
scons distclean
# 启动 8 线程编译
scons -j8
```
#### SDL2 模拟运行
```bash
cd dist/
./HelloWorld
```


#### 交叉编译
```bash
# 安装交叉编译工具
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
# 进入工程目录
cd projects/HelloWorld
# 加载配置项
export CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk
# 清理环境
scons distclean
# 启动 8 线程编译
scons -j8
```


### macOS
#### 依赖安装（仅需执行一次）

```bash
python3 -m venv launcher-python-venv
source launcher-python-venv/bin/activate
pip3 install parse scons requests tqdm
pip3 install setuptools-rust paramiko scp
```

> Python 版本需 ≥ 3.8

#### 克隆项目（含子模块）

```bash
git clone --recursive https://github.com/CardputerZero/launcher.git
cd launcher
```

如果已克隆但未初始化子模块：

```bash
git submodule update --init --recursive
```
进入 `HelloWorld` 进行编译：


#### 交叉编译
```bash
# 安装交叉编译工具
brew tap messense/macos-cross-toolchains
brew install aarch64-unknown-linux-gnu
# 进入工程目录
cd projects/HelloWorld
# 加载配置项
export CONFIG_DEFAULT_FILE=mac_cross_cp0_config_defaults.mk
# 清理环境
scons distclean
# 启动 8 线程编译
scons -j8
```

### Windows

Windows 使用 `projects/APPLaunch` 下同一套 SCons 入口。编译本机 SDL2 仿真器时，建议使用 MSYS2 MinGW Shell，确保 `gcc`、`g++`、`pkg-config`、SDL2 和 FreeType 都在 `PATH` 中。

#### 依赖安装（MSYS2 UCRT64 示例）

```bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-SDL2 \
  mingw-w64-ucrt-x86_64-freetype \
  mingw-w64-ucrt-x86_64-python-pip

python -m pip install parse scons requests tqdm setuptools-rust paramiko scp
```

#### 编译 APPLaunch SDL2 仿真器

```bash
cd projects/APPLaunch
scons distclean
export CONFIG_DEFAULT_FILE=win_x86_sdl2_config_defaults.mk
scons -j8
cd dist
./M5CardputerZero-APPLaunch.exe
```

#### 交叉编译 APPLaunch 设备端程序

安装 SysGCC Raspberry64 Windows AArch64 Linux 交叉工具链：`https://sysprogs.com/getfile/2542/raspberry64-gcc14.2.0.exe`。如果工具链未安装在 `D:\app\SysGCC\bin`，请同步修改 `projects/APPLaunch/win_x86_cross_config_defaults.mk` 中的 `CONFIG_TOOLCHAIN_PATH`。

```bash
cd projects/APPLaunch
scons distclean
export CONFIG_DEFAULT_FILE=win_x86_cross_config_defaults.mk
scons -j8
```

交叉编译产物为 `dist/M5CardputerZero-APPLaunch`，用于在 CardputerZero 设备上运行。首次交叉编译可能会下载 SDK sysroot 到 `SDK/github_source/static_lib_v0.0.6`。



### 配置管理命令

```bash
# 查看/修改编译配置（图形化菜单）
scons menuconfig

# 清理编译产物
scons -c

# 彻底清理（含配置缓存）
scons distclean

# 配置 setup.ini 中的主机 IP 和操作命令后，可使用 scons push 将文件传输到设备并执行指定命令。
scons push
```

---

## APPLaunch
该程序是运行在 CardputerZero 设备上的桌面 UI 程序，为 LCD 小屏幕提供基础操作界面。

### 编译
设备端编译、SDL2 仿真器和交叉编译均使用上方平台相关命令，并进入 `projects/APPLaunch` 执行。APPLaunch 提供独立配置文件，包括 `linux_x86_sdl2_config_defaults.mk`、`linux_x86_cross_cp0_config_defaults.mk`、`mac_cross_cp0_config_defaults.mk`、`win_x86_sdl2_config_defaults.mk` 和 `win_x86_cross_config_defaults.mk`。

### 打包
```bash
# 可选：安装 dpkg 后会使用 dpkg-deb；否则使用 Python 写包器。
python3 scripts/debian_packager.py
```
该命令会生成一个 DEB 安装包。将安装包通过 `scp` 传输到设备后，可使用 `sudo dpkg -i ./***.deb` 进行安装。

### 运行

#### 自动运行
使用 `dpkg` 安装 DEB 包后，程序会通过 `systemd` 用户服务 `APPLaunch.service` 自动启动。

安装后使用 `systemd` 命令进行查看和操作：
```bash
pi@pi:~ $ systemctl --user status APPLaunch.service
● APPLaunch.service - APPLaunch Service
     Loaded: loaded (/usr/lib/systemd/user/APPLaunch.service; enabled; preset: enabled)
     Active: active (running) since Mon 2026-06-08 15:58:19 CST; 23min ago
 Invocation: aa5c4e3ca94742deb2fe0dc67467e670
   Main PID: 664 (M5CardputerZero)
      Tasks: 7 (limit: 448)
        CPU: 1min 19.265s
     CGroup: /user.slice/user-1000.slice/user@1000.service/app.slice/APPLaunch.service
             └─664 /usr/share/APPLaunch/bin/M5CardputerZero-APPLaunch

Jun 08 15:58:19 pi systemd[664]: Started APPLaunch.service - APPLaunch Service.
```

#### 手动运行
在设备上运行（使用 Framebuffer）：

```bash
# 自动检测 ST7789V Framebuffer 设备
./dist/M5CardputerZero-APPLaunch

# 或手动指定 Framebuffer 设备
export LV_LINUX_FBDEV_DEVICE=/dev/fb0
./dist/M5CardputerZero-APPLaunch

# 指定键盘输入设备
export LV_LINUX_KEYBOARD_DEVICE=/dev/input/by-path/platform-3f804000.i2c-event
./dist/M5CardputerZero-APPLaunch
```


### UI 界面说明

界面由 SquareLine Studio 1.5.0 设计生成，分辨率为 **320×170**（ST7789V 屏幕）。

### APPLaunch 界面

| 区域 | 内容 |
|------|------|
| 顶部状态栏 | Logo、时钟时间、电量百分比、WiFi 信息 |
| 主内容区 | 应用轮播页面（左右翻页导航） |
| 全局提示 | ESC / Shift / SYM 快捷键提示覆盖层 |

APPLaunch 在所有构建中注册 Python、Store、CLI/ST 终端、Game、Setting、Math 和 LoRa；Linux 设备构建另外注册 IP Panel、SSH 和 Tank Battle。Python、Store、Math 是命令或外部进程入口，其余为进程内页面。


## 相关资源

- [日文工程指南](./docs/launcher-project-guide-ja.md)
- [日文 APPLaunch App 打包指南](./docs/APPLaunch-App-packaging-guide-ja.md)
- [日文 macOS Docker 编译指南](./docs/macos-docker-build-ja.md)
- [M5Stack_Linux_Libs SDK](https://github.com/m5stack/M5Stack_Linux_Libs)
- [LVGL 文档](https://docs.lvgl.io/)
- [SquareLine Studio](https://squareline.io/)
- [M5CardputerZero 产品页](https://docs.m5stack.com/)
