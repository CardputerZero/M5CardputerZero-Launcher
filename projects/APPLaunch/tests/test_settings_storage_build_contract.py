from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
KCONFIG = (ROOT / "main/Kconfig").read_text(encoding="utf-8")
SCONSTRUCT = (ROOT / "main/SConstruct").read_text(encoding="utf-8")
MODEL = (ROOT / "main/ui/settings/settings_storage_model.cpp").read_text(
    encoding="utf-8"
)
PAGE = (ROOT / "main/ui/settings/settings_static_info_page.cpp").read_text(
    encoding="utf-8"
)


def storage_config_block() -> str:
    start = KCONFIG.index("config APPLAUNCH_TARGET_SD_MMC_STORAGE")
    end = KCONFIG.index("endmenu", start)
    return KCONFIG[start:end]


def test_storage_probe_macro_is_device_only():
    block = storage_config_block()
    assert "config APPLAUNCH_TARGET_SD_MMC_STORAGE" in block
    assert "\n    bool\n" in block
    for target in (
        "APPLAUNCH_LINUX_CP0",
        "APPLAUNCH_LINUX_X86_CROSS_CP0",
        "APPLAUNCH_MAC_CROSS_CP0",
        "APPLAUNCH_WIN_X86_CROSS_CP0",
    ):
        assert target in block
    assert (
        "depends on APPLAUNCH_LINUX_CP0 || APPLAUNCH_LINUX_X86_CROSS_CP0 || "
        "APPLAUNCH_MAC_CROSS_CP0 || APPLAUNCH_WIN_X86_CROSS_CP0"
    ) in block
    assert "\n    default y\n" in block
    assert "APPLAUNCH_LINUX_X86_SDL2" not in block
    assert "APPLAUNCH_WIN_X86_SDL2" not in block
    assert "APPLAUNCH_DARWIN_SDL" not in block


def test_storage_macro_is_not_defined_independently_by_sconstruct():
    assert "APPLAUNCH_TARGET_SD_MMC_STORAGE" not in SCONSTRUCT
    assert "Only CP0 target builds may probe Linux MMC mounts" not in SCONSTRUCT


def test_native_probe_requires_linux_and_sd_sysfs_type():
    assert MODEL.count(
        "defined(CONFIG_APPLAUNCH_TARGET_SD_MMC_STORAGE) && defined(__linux__)"
    ) == 2
    assert "defined(APPLAUNCH_TARGET_SD_MMC_STORAGE)" not in MODEL
    assert 'std::ifstream mounts("/proc/self/mounts")' in MODEL
    assert 'std::filesystem::path("/sys/class/block")' in MODEL
    assert 'if (type == "SD")' in MODEL
    assert 'if (type == "MMC")' in MODEL


def test_storage_page_labels_available_capacity_as_available():
    assert '"SD card"' in PAGE
    assert '"Total: " + SettingsStorageModel::format_bytes' in PAGE
    assert '"Available: " + SettingsStorageModel::format_bytes' in PAGE
    assert '"Free: " + SettingsStorageModel::format_bytes' not in PAGE


if __name__ == "__main__":
    test_storage_probe_macro_is_device_only()
    test_storage_macro_is_not_defined_independently_by_sconstruct()
    test_native_probe_requires_linux_and_sd_sysfs_type()
    test_storage_page_labels_available_capacity_as_available()
