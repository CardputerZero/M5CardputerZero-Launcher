#include "cp0_bluetooth_backend.hpp"
#include "cp0_bluez_dbus_client.hpp"

namespace cp0_bluetooth_backend {

cp0_bt_status_t status()
{
    return cp0_bluez_dbus::status();
}

int set_power(int enabled)
{
    return cp0_bluez_dbus::set_power(enabled);
}

int set_alias(const char *alias)
{
    return cp0_bluez_dbus::set_alias(alias);
}

int set_discoverable(int enabled)
{
    return cp0_bluez_dbus::set_discoverable(enabled);
}

int start_discovery()
{
    return cp0_bluez_dbus::start_discovery();
}

int stop_discovery()
{
    return cp0_bluez_dbus::stop_discovery();
}

int scan(cp0_bt_device_t *out, int max_devices)
{
    if (!out || max_devices <= 0)
        return 0;
    if (start_discovery() != 0)
        return -1;
    return list(out, max_devices, false);
}

int list(cp0_bt_device_t *out, int max_devices, bool connected_only)
{
    return cp0_bluez_dbus::list(out, max_devices, connected_only);
}

int pair(const char *address)
{
    return cp0_bluez_dbus::pair(address);
}

int connect(const char *address)
{
    return cp0_bluez_dbus::connect(address);
}

int disconnect(const char *address)
{
    return cp0_bluez_dbus::disconnect(address);
}

int remove(const char *address)
{
    return cp0_bluez_dbus::remove(address);
}

} // namespace cp0_bluetooth_backend
