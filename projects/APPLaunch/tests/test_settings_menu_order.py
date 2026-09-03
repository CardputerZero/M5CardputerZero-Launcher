import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SETTINGS_PAGE = (ROOT / "main/ui/settings/settings_page.cpp").read_text()
ETHERNET_CONTROLLER = (
    ROOT / "main/ui/settings/settings_ethernet_controller.cpp"
).read_text()
SETTINGS_ROLLER = (ROOT / "main/ui/settings/settings_menu_roller.hpp").read_text()
WIFI_PAGE = (ROOT / "main/ui/settings/settings_wifi_page.cpp").read_text()
SYSTEM_PAGE = (ROOT / "main/ui/settings/settings_system_page.cpp").read_text()
SETTINGS_ADAPTER = (ROOT / "main/ui/settings/settings_adapter.cpp").read_text()
BUILTIN_REGISTRY = (ROOT / "main/ui/builtin_app_registry.cpp").read_text()
PREINSTALLED_MANIFEST = (ROOT / "APPLaunch/preinstalled-desktop-apps.tsv").read_text()

EXPECTED_ROOT_ORDER = [
    "Screen",
    "Speaker",
    "Wi-Fi",
    "Ethernet",
    "Bluetooth",
    "ExtPort",
    "Battery",
    "Launcher",
    "Developer",
    "User",
    "Date & Time",
    "System",
]


def settings_tree_builder() -> str:
    start = SETTINGS_PAGE.index("void UISettingTreePage::create_page_detail()")
    end = SETTINGS_PAGE.index("void UISettingTreePage::back_home", start)
    body = SETTINGS_PAGE[start:end]
    return re.sub(r"//.*", "", body)


def test_root_menu_matches_product_order():
    root_labels = re.findall(
        r'mode_tree\.append_child\(\s*root,\s*SettingEntry\{"([^"]+)"',
        settings_tree_builder(),
    )
    assert root_labels == EXPECTED_ROOT_ORDER


def test_screen_remains_the_initial_selection():
    assert "selected_index = 0;" in SETTINGS_ROLLER
    assert "selected_index = 2;" not in SETTINGS_ROLLER


def test_volume_opens_the_volume_editor_directly():
    builder = settings_tree_builder()
    assert re.search(
        r'root,\s*SettingEntry\{"Speaker",\s*volume_page3_factory,\s*PageType::FullCustom\}',
        builder,
    )
    assert not re.search(r'append_child\(\s*speaker,\s*SettingEntry\{"Volume"', builder)


def test_wifi_menu_uses_product_labels_without_changing_actions():
    wifi_entries = re.findall(
        r'append_child\(\s*wifi,\s*SettingEntry\{"([^"]+)"',
        settings_tree_builder(),
    )
    assert wifi_entries == ["Enable", "Networks", "Join Hidden Network"]
    assert 'SettingEntry{"Enable", wifi_power_api, true}' in SETTINGS_PAGE
    assert 'SettingEntry{"Networks", wifi_scan_page3_factory, PageType::FullCustom}' in SETTINGS_PAGE
    assert 'SettingEntry{"Join Hidden Network", wifi_add_hidden_page_factory, PageType::FullCustom}' in SETTINGS_PAGE
    assert 'create_label(hidden_panel_, "Join Hidden Network"' in WIFI_PAGE


def test_ethernet_is_a_submenu_with_enable_and_existing_info_page():
    ethernet_entries = re.findall(
        r'append_child\(\s*ethernet,\s*SettingEntry\{"([^"]+)"',
        settings_tree_builder(),
    )
    assert ethernet_entries == ["Enable", "Info"]
    assert 'SettingEntry{"Enable", ethernet_enabled_api, true}' in SETTINGS_PAGE
    assert 'SettingEntry{"Info", settings_ethernet_page_factory, PageType::FullCustom}' in SETTINGS_PAGE
    assert '"GENERAL.STATE", "device", "show", "eth0"' in ETHERNET_CONTROLLER
    assert "GENERAL.AUTOCONNECT" not in SETTINGS_PAGE
    assert "GENERAL.AUTOCONNECT" not in ETHERNET_CONTROLLER
    assert 'operation == Operation::Connect ? "connect" : "disconnect"' in ETHERNET_CONTROLLER
    assert 'create_network_table_row(ComponensObj, 0, "IP")' in SYSTEM_PAGE
    assert 'create_network_table_row(ComponensObj, 1, "Gateway")' in SYSTEM_PAGE
    assert 'create_network_table_row(ComponensObj, 2, "MAC")' in SYSTEM_PAGE
    assert 'settings_fonts::mono(14)' in SYSTEM_PAGE


def test_ext_port_order_and_hardware_mappings():
    ext_port_entries = re.findall(
        r'append_child\(\s*ext_port,\s*SettingEntry\{"([^"]+)"',
        settings_tree_builder(),
    )
    assert ext_port_entries == ["Ext 5V", "Grove 5V"]
    assert 'SettingEntry{"Ext 5V", std::bind(&ext_port_com, "EXT5V"' in SETTINGS_PAGE
    assert 'SettingEntry{"Grove 5V", std::bind(&ext_port_com, "GROVE5V"' in SETTINGS_PAGE


def test_launcher_settings_use_builtin_and_release_preinstalled_apps():
    assert SETTINGS_ADAPTER.count("launcher_app_registry_entries(&count)") == 2
    assert "launcher_builtin_app_registry_entries(&count)" not in SETTINGS_ADAPTER
    assert "launcher::is_launcher_settings_origin(descriptor.origin)" in SETTINGS_ADAPTER
    assert "launcher::is_protected_launcher_app(descriptor.config_key)" in SETTINGS_ADAPTER
    assert '{"Python", "python_100.png", "app_Python", true, false}' in BUILTIN_REGISTRY
    assert '{"Snake", "game_100.png", "app_Game", true, false}' in BUILTIN_REGISTRY
    assert (
        "zclaw.desktop\tZClaw\tshare/images/claw_100.png\t"
        "/usr/share/APPLaunch/bin/ZClaw\tfalse\ttrue"
    ) in PREINSTALLED_MANIFEST


def test_system_replaces_about_with_requested_entries():
    builder = settings_tree_builder()
    system = builder[builder.index("NodeIter system =") :]
    system_entries = re.findall(
        r'append_child\(\s*system,\s*SettingEntry\{"([^"]+)"',
        system,
    )
    assert system_entries == ["Software", "Storage", "Credit"]
    assert "settings_update_page_factory" in system
    assert "settings_storage_page_factory" in system
    assert "settings_credit_page_factory" in system
    assert "boot_actions::Action::Reboot" in system
    for removed in ('SettingEntry{"OS"', 'SettingEntry{"APPLaunch"',
                    'SettingEntry{"Help"', 'SettingEntry{"Boot"'):
        assert removed not in system


if __name__ == "__main__":
    test_root_menu_matches_product_order()
    test_screen_remains_the_initial_selection()
    test_volume_opens_the_volume_editor_directly()
    test_wifi_menu_uses_product_labels_without_changing_actions()
    test_ethernet_is_a_submenu_with_enable_and_existing_info_page()
    test_ext_port_order_and_hardware_mappings()
    test_launcher_settings_use_builtin_and_release_preinstalled_apps()
    test_system_replaces_about_with_requested_entries()
