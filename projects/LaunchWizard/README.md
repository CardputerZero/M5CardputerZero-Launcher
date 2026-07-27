# LaunchWizard

LaunchWizard is the CardputerZero first-boot setup application. Its own code uses
a conventional C++ project layout; the hardware GUI is integrated through the
repository SDK's SCons component system.

## Layout

```text
main/src/               Application and LVGL runtime entry points
main/ui/wizard_model.*  UI-independent Model and validation rules
main/ui/wizard_view.cpp LVGL View and interaction coordinator
main/ui/wizard_service.* Platform Service implementation
tests/                  Host-side unit tests
main/                   Thin SDK/SCons component adapter
```

## Host build and tests

The reusable, platform-independent code is a regular CMake target and does not
require the device SDK:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Consumers can link the `LaunchWizard::core` target through `add_subdirectory`
or an installed `find_package(LaunchWizard CONFIG REQUIRED)` package.

## GUI application

The complete executable depends on the repository-specific LVGL, display,
keyboard, audio, and radio components. Build it from this directory using the
SDK entry point:

```sh
# Native SDL development build (default on x86_64 Linux)
scons -j8

# CardputerZero cross build
CardputerZero=y scons -j8
```

The device service contract is:

```text
ExecStart=/usr/share/APPLaunch/bin/LaunchWizard
WorkingDirectory=/usr/share/APPLaunch
```

`setup.ini` installs `dist/LaunchWizard` at that exact path and prints both
SHA-256 values. It intentionally does not stop or restart the service while
deploying; activation is an explicit device-side operation.

Both build paths follow the same `main/src` and `main/ui` layout as the
HelloWorld reference project.

The SDL acceptance pages can be opened directly with:

```sh
./dist/LaunchWizard --preview-configuring
./dist/LaunchWizard --preview-restart
```

The UI follows an MSV boundary: `WizardModel` owns setup state and validation,
platform operations are isolated from it, and the LVGL source owns view objects
and translates input events into model changes.
