#!/usr/bin/env python3

import argparse
from pathlib import Path


def parse_desktop(path: Path) -> tuple[str, str, str, str, str]:
    values: dict[str, str] = {}
    in_desktop_entry = False
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        if not raw_line or raw_line[0] in "#;":
            continue
        if raw_line[0] == "[":
            in_desktop_entry = raw_line == "[Desktop Entry]"
            continue
        if not in_desktop_entry or "=" not in raw_line:
            continue
        key, value = raw_line.split("=", 1)
        values[key.strip()] = value.strip()

    if not values.get("Name") or not values.get("Exec"):
        raise ValueError(f"{path}: Desktop Entry requires Name and Exec")
    if values.get("Type", "Application") != "Application":
        raise ValueError(f"{path}: Desktop Entry is not an Application")
    parse_bool = lambda value: value in {"true", "True", "1"}
    if parse_bool(values.get("Hidden", "false")) or parse_bool(
        values.get("NoDisplay", "false")
    ):
        raise ValueError(f"{path}: Desktop Entry is hidden")
    return (
        values["Name"],
        values.get("Icon", ""),
        values["Exec"],
        "true" if parse_bool(values.get("Terminal", "false")) else "false",
        "true" if parse_bool(values.get("Sysplause", "true")) else "false",
    )


def manifest_identity(path: Path, filename: str) -> tuple[str, str, str, str, str]:
    for line_number, raw_line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        if not raw_line or raw_line.startswith("#"):
            continue
        fields = raw_line.split("\t")
        if len(fields) != 6 or fields[0] != filename:
            continue
        if fields[4] not in {"true", "false"} or fields[5] not in {
            "true",
            "false",
        }:
            raise ValueError(f"{path}:{line_number}: invalid boolean field")
        return tuple(fields[1:])
    raise ValueError(f"{path}: no valid manifest entry for {filename}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("desktop", type=Path)
    parser.add_argument("filename")
    args = parser.parse_args()

    expected = manifest_identity(args.manifest, args.filename)
    actual = parse_desktop(args.desktop)
    if expected != actual:
        labels = ("Name", "Icon", "Exec", "Terminal", "Sysplause")
        differences = ", ".join(
            f"{label}: manifest={wanted!r}, desktop={found!r}"
            for label, wanted, found in zip(labels, expected, actual)
            if wanted != found
        )
        raise ValueError(f"{args.filename}: preinstalled identity mismatch ({differences})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
