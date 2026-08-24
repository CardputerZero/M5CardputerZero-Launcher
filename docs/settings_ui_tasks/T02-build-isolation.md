# T02：构建、运行入口与 worker 隔离

**固定 worker 目录**：`/home/nihao/w2T/github/launcher/projects/ui_test2`

## 目标

保证每名程序员在独立副本中修改、编译，不污染主工程、其他 worker 的源码和 build 输出。

## 负责范围

- `projects/ui_test/SConstruct`
- `projects/ui_test/main/SConstruct`
- 各 worker 的配置和静态资源布局。
- 必要时提供 worker 创建脚本和构建说明。

## worker 目录

统一使用：

- `/home/nihao/w2T/github/launcher/projects/ui_test1`
- `/home/nihao/w2T/github/launcher/projects/ui_test2`
- `/home/nihao/w2T/github/launcher/projects/ui_test3`

不要重复拼接完整的 `/home/nihao/w2T/github/launcher/projects/ui_test` 路径。

## 工作内容

1. 验证 `cp -a ui_test ui_testN` 后资源和源码路径正确。
2. 验证 `main/ui`、APPLaunch 静态资源和符号链接不会指向其他 worker。
3. 保证每个副本使用独立 `build`、`.cache` 和生成配置。
4. 提供 SDL 本机和 CP0 交叉构建命令。
5. 记录工具链、SDK、静态库版本要求。

## 验证命令

```sh
cd /home/nihao/w2T/github/launcher/projects/ui_test2
export CONFIG_DEFAULT_FILE=linux_x86_sdl2_config_defaults.mk
scons -j8
export CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk
scons -j8
```

## 验收

- 两种配置切换不会复用错误的生成配置。
- worker build 输出不出现在主工程。
- 新建两个 worker 并行构建不会互相覆盖文件。
- 提供总工可直接复制的 worker 模板和说明。
