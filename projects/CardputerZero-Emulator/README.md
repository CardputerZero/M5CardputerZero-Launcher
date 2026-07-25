# Cardputer Zero Emulator

The Cardputer Zero Emulator runs the launcher UI on a desktop with the same
320×170 framebuffer used by the device. The native build includes a clickable
Cardputer skin, physical keyboard input, APPLaunch, and selected desktop
runtime services.

## Build on macOS

Install the build dependencies:

```sh
brew install cmake sdl2 sdl2_image freetype pkg-config nlohmann-json
python3 -m pip install --user parse requests tqdm
```

From the repository root, initialize the SDK and LVGL sources:

```sh
git submodule update --init --depth=1 SDK
git clone --depth 1 --branch v9.5.0 \
  https://github.com/lvgl/lvgl.git \
  projects/CardputerZero-Emulator/lib/lvgl
```

If `projects/CardputerZero-Emulator/lib/lvgl` already exists, do not clone it
again. Configure, build, and test:

```sh
cmake -S projects/CardputerZero-Emulator \
  -B projects/CardputerZero-Emulator/build \
  -DCMAKE_BUILD_TYPE=Release
cmake --build projects/CardputerZero-Emulator/build --parallel
ctest --test-dir projects/CardputerZero-Emulator/build --output-on-failure
```

Run the emulator from its build directory so it can find the packaged apps and
assets:

```sh
cd projects/CardputerZero-Emulator/build
./cardputer-zero-emu
```

## Controls

- Use the computer keyboard, or click the keys and side buttons on the device.
- `Aa`, `fn`, `ctrl`, `alt`, and `SYM` are toggled when clicked.
- `ESC`, `HOME`, `TALK`, and `NEXT` are clickable side buttons.
- The first click or key press while the screensaver is active wakes the
  display without also activating the selected control.
- Clicking the power switch opens the emulator reset prompt.

## Display scaling

The default 1280×840 window displays the 320×170 LCD at an exact 2× scale. The
window is resizable and SDL preserves the device aspect ratio.

Set `EMU_WINDOW_SCALE` to choose the initial window size. Values from `0.5` to
`2.0` are supported:

```sh
EMU_WINDOW_SCALE=0.5 ./cardputer-zero-emu  # compact 640×420 window
EMU_WINDOW_SCALE=1.5 ./cardputer-zero-emu  # larger 1920×1260 window
```

## Automated tests

On macOS, CTest runs headless regressions that cover:

- native runtime startup and reset;
- clickable on-screen controls;
- keyboard navigation; and
- waking APPLaunch from its screensaver.
