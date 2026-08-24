# Settings UI Worker

`projects/ui_test` is the source template for the Settings UI worker copies. A
worker keeps all generated files below its own directory; it does not use the
main project's `build`, `dist`, `.cache`, or generated configuration files.

## Create a worker

From the launcher repository:

```bash
./scripts/create_settings_ui_worker.sh \
  /home/nihao/w2T/github/launcher/projects/ui_test2
```

The helper performs the requested `cp -a ui_test ui_testN` operation and then
removes any copied generated files (`build`, `dist`, `.cache`, `SDK`,
`compile_commands.json`, and root SCons/Kconfig state). It also checks that
`main/ui`, `APPLaunch`, and all source/resource symlinks resolve inside the new
worker.

If the worker is copied manually, use the equivalent cleanup and validation:

```bash
cd /home/nihao/w2T/github/launcher/projects
cp -a ui_test ui_test2
rm -rf ui_test2/build ui_test2/dist ui_test2/.cache ui_test2/SDK \
  ui_test2/compile_commands.json ui_test2/.sconsign.dblite \
  ui_test2/.config ui_test2/.config.old ui_test2/.config.mk
./ui_test2/validate_worker.sh ui_test2
```

Workers should stay in the same repository layout so the build can discover
`SDK` and `ext_components`. For a worker stored elsewhere, set the workspace
root explicitly:

```bash
export CARDPUTERZERO_ROOT=/home/nihao/work/launcher
```

Custom defaults files must also be copied into the worker; an absolute defaults
file from the main project is rejected to prevent accidental cross-worker reuse.

The source tree's `projects/APPLaunch/main/ui/settings` link is intentionally a
relative link to the Settings UI source. Do not copy that link into a worker
from another checkout. When validating a full APPLaunch integration checkout,
pass its link as the second argument and require it to resolve to that worker's
`main/ui`:

```bash
./ui_test2/validate_worker.sh \
  ui_test2 \
  /home/nihao/w2T/github/launcher/projects/APPLaunch/main/ui/settings
```

## Build and run

The copied worker always emits the executable as `ui_test`, regardless of the
worker directory suffix. This keeps `APPLaunch/applications/ui_test.desktop`
valid in every copy.

SDL2 native build:

```bash
cd /home/nihao/w2T/github/launcher/projects/ui_test2
./worker.sh sdl
cd dist
./ui_test
```

The equivalent direct SCons invocation is:

```bash
cd /home/nihao/w2T/github/launcher/projects/ui_test2
export CONFIG_DEFAULT_FILE=linux_x86_sdl2_config_defaults.mk
scons -j8
```

CP0 cross build:

```bash
cd /home/nihao/w2T/github/launcher/projects/ui_test2
./worker.sh cp0
file dist/ui_test
```

The equivalent direct SCons invocation is:

```bash
cd /home/nihao/w2T/github/launcher/projects/ui_test2
export CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk
scons -j8
```

Switching between `sdl` and `cp0` is safe without a manual clean. The
`SConstruct` records the absolute defaults-file path and file contents in
`build/config/selected-defaults.txt`; a change removes old configuration,
objects, executable output, and compile commands before regenerating them.

Two workers can build concurrently because each one uses only:

```text
<worker>/build
<worker>/build/config
<worker>/dist
<worker>/.cache
```

The source/resource link check intentionally does not reject a separately
provisioned `SDK` dependency cache; it only protects `main` and `APPLaunch`
paths from resolving into another worker.

The CP0 SDK static library is a read-only shared dependency, not a worker build
output. Its required version comes from
`ext_components/cp0_lvgl/sdk_version.txt` (currently `v0.0.6`). If it is not
already present under `SDK/github_source`, the first CP0 build downloads the
matching SDK BSP package.

## Toolchain requirements

The tested environment on 2026-08-24 used Python 3.12.3, SCons 4.11.0, and
GCC 13.3.0. The project requires:

- Python 3.10 or newer with SCons 4.x and the SDK Python dependencies (`parse`,
  `requests`, `tqdm`, `paramiko`, and `scp`).
- SDL2 development headers/libraries and FreeType for the native SDL build.
- `aarch64-linux-gnu-gcc` and `aarch64-linux-gnu-g++` for the CP0 build.
- The CP0 SDK static library matching the version in
  `ext_components/cp0_lvgl/sdk_version.txt`.

Use `./worker.sh info` to print the locally installed tool versions.

The SDK-independent isolation regression test is:

```bash
./tests/test_build_isolation.sh
```
