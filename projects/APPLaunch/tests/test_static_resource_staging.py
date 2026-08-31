#!/usr/bin/env python3
import ast
import shutil
import tempfile
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
SCONSTRUCT = PROJECT_ROOT / "main" / "SConstruct"


def staging_ignore_patterns():
    tree = ast.parse(SCONSTRUCT.read_text(encoding="utf-8"))
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        if any(
            isinstance(target, ast.Name)
            and target.id == "_static_copy_ignore_patterns"
            for target in node.targets
        ):
            return ast.literal_eval(node.value)
    raise AssertionError("static resource ignore patterns are not configured")


def test_static_copy_uses_the_configured_ignore_patterns():
    tree = ast.parse(SCONSTRUCT.read_text(encoding="utf-8"))
    copy_calls = [
        node
        for node in ast.walk(tree)
        if isinstance(node, ast.Call)
        and isinstance(node.func, ast.Attribute)
        and isinstance(node.func.value, ast.Name)
        and node.func.value.id == "shutil"
        and node.func.attr == "copytree"
        and len(node.args) >= 2
        and isinstance(node.args[0], ast.Name)
        and node.args[0].id == "_static_source"
        and isinstance(node.args[1], ast.Name)
        and node.args[1].id == "_staged_applaunch"
    ]
    assert len(copy_calls) == 1

    ignore_keywords = [
        keyword.value for keyword in copy_calls[0].keywords if keyword.arg == "ignore"
    ]
    assert len(ignore_keywords) == 1
    ignore_call = ignore_keywords[0]
    assert isinstance(ignore_call, ast.Call)
    assert isinstance(ignore_call.func, ast.Attribute)
    assert isinstance(ignore_call.func.value, ast.Name)
    assert ignore_call.func.value.id == "shutil"
    assert ignore_call.func.attr == "ignore_patterns"
    assert len(ignore_call.args) == 1
    assert isinstance(ignore_call.args[0], ast.Starred)
    assert isinstance(ignore_call.args[0].value, ast.Name)
    assert ignore_call.args[0].value.id == "_static_copy_ignore_patterns"


def test_static_copy_excludes_python_cache_artifacts_only():
    patterns = staging_ignore_patterns()
    assert "__pycache__" in patterns
    assert "*.pyc" in patterns
    assert "*.pyo" in patterns

    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        source = root / "APPLaunch"
        target = root / "staged" / "APPLaunch"
        (source / "bin" / "__pycache__").mkdir(parents=True)
        (source / "share" / "images").mkdir(parents=True)
        (source / "bin" / "helper.py").write_text("pass\n")
        (source / "bin" / "legacy.pyc").write_bytes(b"bytecode")
        (source / "bin" / "legacy.pyo").write_bytes(b"optimized bytecode")
        (source / "bin" / "__pycache__" / "module.cpython-314.pyc").write_bytes(
            b"cached bytecode"
        )
        (source / "share" / "images" / "icon.png").write_bytes(b"png")

        shutil.copytree(
            source,
            target,
            ignore=shutil.ignore_patterns(*patterns),
        )

        assert (target / "bin" / "helper.py").is_file()
        assert (target / "share" / "images" / "icon.png").is_file()
        assert not (target / "bin" / "legacy.pyc").exists()
        assert not (target / "bin" / "legacy.pyo").exists()
        assert not (target / "bin" / "__pycache__").exists()


if __name__ == "__main__":
    test_static_copy_uses_the_configured_ignore_patterns()
    test_static_copy_excludes_python_cache_artifacts_only()
