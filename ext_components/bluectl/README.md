# bluectl

BlueZ Bluetooth control library — a pure C API for controlling Bluetooth through
the system DBus (`org.bluez`). It wraps commonly used `bluetoothctl`
capabilities as a library. Built on `libdbus-1` (pure C, with no glib
dependency), following the component structure used by
`SDK/components/DeviceDriver`.

## Features

| Group | Capabilities | Corresponding bluetoothctl commands |
| ---- | ---- | ----------------- |
| Adapter | Enumerate/query/power/discoverable/pairable/alias/timeout/scan | `list` `power` `discoverable` `pairable` `scan` `set-alias` `timeout` |
| Device | Enumerate/query/pair/connect/disconnect/trust/block/remove/rename | `devices` `info` `pair` `connect` `disconnect` `trust` `block` `remove` `set-alias` |
| Events | Callbacks for object add/remove, property changes, and BlueZ start/stop | (bluetoothctl interactive prompts) |
| Agent | Callback-based pairing agent for PIN/passkey/confirmation/authorization | `agent on` `default-agent` |
| Media | AVRCP player status queries and controls | (media) |

## Directory Layout

```
bluectl/
├── Kconfig               Component configuration options
├── SConstruct            SCons component build script
└── src/
    ├── bluectl.h         Public API (the only external header)
    ├── bluectl_internal.h
    ├── bluectl_core.c    DBus connection/event loop/common utilities
    ├── bluectl_adapter.c Adapter operations
    ├── bluectl_device.c  Device operations
    ├── bluectl_agent.c   Pairing agent (CONFIG_BLUECTL_AGENT_ENABLED)
    └── bluectl_media.c   Media controls (CONFIG_BLUECTL_MEDIA_ENABLED)
```

## Dependencies

- Build: `libdbus-1` headers (cross-compilation uses the
  `usr/include/dbus-1.0` headers and `libdbus-1.so.3` from the SDK
  `static_lib_*` sysroot; native builds use `pkg-config dbus-1`).
- Runtime: `dbus` + `bluez` (`bluetoothd`). The calling process must have
  sufficient permissions (root, or membership in the `bluetooth`/`dbus`
  groups, depending on the distribution policy).

## Enablement

Add the following to the project's `config_defaults.mk` (or select the options
with `scons menuconfig`):

```ini
CONFIG_BLUECTL_COMPONENT_ENABLED=y
CONFIG_BLUECTL_AGENT_ENABLED=y     # Pairing agent, enabled by default
CONFIG_BLUECTL_MEDIA_ENABLED=n     # AVRCP media controls, disabled by default
```

The component is named `bluectl`; add the following to the consumer's
`SConstruct`:

```python
REQUIREMENTS = ['bluectl']
```

In code:

```c
#include "bluectl.h"
```

## Usage Example

```c
#include <stdio.h>
#include "bluectl.h"

static void on_event(const bluectl_event_t *ev, void *user)
{
        printf("[ev] type=%d path=%s iface=%s prop=%s val=%s\n",
               ev->type, ev->path ? ev->path : "-", ev->interface ? ev->interface : "-",
               ev->property ? ev->property : "-", ev->value ? ev->value : "-");
}

static void on_agent(const bluectl_agent_request_t *req, void *user)
{
        if (!req->needs_reply) {
                printf("[agent] %s device=%s passkey=%s\n",
                       req->method, req->device, req->passkey);
                return;
        }
        if (!strcmp(req->method, "RequestConfirmation"))
                printf("Confirm pairing %s passkey=%s? Press Enter to confirm\n",
                       req->device, req->passkey);
        /* Example: accept everything automatically; production code should
         * collect user input before replying. */
        bluectl_agent_reply(req->id, 1, "1234");
}

int main(void)
{
        bluectl_device_t devs[32];
        int n, i;

        if (bluectl_init() != BLUECTL_OK) {
                printf("init failed: %s\n", bluectl_last_error());
                return 1;
        }
        bluectl_set_event_callback(on_event, NULL);
        bluectl_agent_set_callback(on_agent, NULL);
        bluectl_agent_register(NULL);       /* KeyboardDisplay by default */
        bluectl_agent_request_default();

        bluectl_set_power(NULL, 1);         /* power on */
        bluectl_set_discoverable(NULL, 1);
        bluectl_start_discovery(NULL);      /* scan on */

        /* Run the event pump in a dedicated thread or the main loop;
         * agent and event callbacks are triggered here. */
        for (;;) {
                bluectl_process(100);
                /* Example: periodically refresh the device list. */
                n = bluectl_get_devices(NULL, devs, 32);
                for (i = 0; i < n; i++)
                        printf("%s %s conn=%d paired=%d rssi=%d\n",
                               devs[i].address, devs[i].name,
                               devs[i].connected, devs[i].paired, devs[i].rssi);
        }

        bluectl_disconnect("AA:BB:CC:DD:EE:FF");
        bluectl_stop_discovery(NULL);
        bluectl_deinit();
        return 0;
}
```

> Note: All operation APIs are **blocking** DBus calls (`pair` may take up to
> 60s and `connect` up to 30s). Call them from a worker thread rather than the
> UI thread. Event and agent callbacks are driven by `bluectl_process()`, which
> must continue running during pairing.

## API Conventions

- Return values: `BLUECTL_OK(0)` indicates success; negative values are
  `BLUECTL_ERR_*` error codes. See `bluectl_last_error()` for details.
- Adapter arguments: `NULL`/`""` means the default adapter, `"hci0"` is
  expanded automatically, and `"/org/bluez/hci0"` is used as-is.
- Device arguments: `"AA:BB:CC:DD:EE:FF"` (case-insensitive) or a complete
  object path.
- Strings passed to event callbacks are valid only for the duration of the
  callback; copy them if they need to be retained.
