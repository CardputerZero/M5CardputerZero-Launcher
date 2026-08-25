# nmtui

`nmtui` is the `cp0_signal_wifi_api` adapter backed directly by NetworkManager's
`libnm` client library. It does not launch the `nmtui` terminal program and does
not invoke `nmcli`.

## What it provides

The component registers a `libnm` implementation for the Wi-Fi signal declared
by `cp0_lvgl`:

| Signal command | Implementation |
| --- | --- |
| `Status` | Reads active Wi-Fi/Ethernet devices, SSID, signal and IPv4 address |
| `Scan [limit]` | Requests a NetworkManager scan and returns AP/security/saved state |
| `Connect <ssid> [password]` | Activates a saved profile or creates a Wi-Fi profile |
| `ConnectHidden <ssid> [password]` | Creates/activates a hidden Wi-Fi profile |
| `Disconnect` / `ProfileDisconnectActive` | Disconnects the active Wi-Fi device |
| `ProfileForget <ssid>` | Deletes matching NetworkManager profiles |
| `ProfileExists <ssid>` | Checks saved NetworkManager profiles |
| `RadioEnabled` / `RadioSetEnabled <on|off>` | Reads or changes the Wi-Fi radio state |

The existing cp0 payload contract is preserved by using
`cp0_network_api_contract.hpp`; callers of `cp0_wifi_*()` do not need to change.

## Enablement

Enable the component together with `cp0_lvgl`:

```ini
CONFIG_CP0_LVGL_COMPONENT_ENABLED=y
CONFIG_NMTUI_COMPONENT_ENABLED=y
```

`NMTUI_COMPONENT_ENABLED` selects `CP0_LVGL_INIT_WIFI`. When enabled, the
`cp0_lvgl` command-based Wi-Fi implementation is compiled out so that exactly
one callback is registered on `cp0_signal_wifi_api`.

Add the component to the consumer's link requirements:

```python
REQUIREMENTS += ['nmtui']
```

The component exposes `nmtui_wifi_init()` and `nmtui_wifi_deinit()` for users
that manage the service lifecycle explicitly. `cp0_lvgl` calls the compatible
`init_wifi()`/`deinit_wifi()` entry points automatically.

## Dependencies

- Build: `libnm` headers and library, plus GLib/GIO headers.
- Runtime: the `NetworkManager` daemon and its system D-Bus service.
- Cross builds: the component uses `CONFIG_TOOLCHAIN_SYSROOT` and links
  `usr/lib/<triplet>/libnm.so.0` from the selected `static_lib_*` SDK.

The adapter uses synchronous `libnm` queries and a small local GLib main-context
pump around asynchronous activation calls. Wi-Fi operations should still be
invoked from a worker thread rather than the LVGL rendering thread when the
operation may block on association or DHCP.
