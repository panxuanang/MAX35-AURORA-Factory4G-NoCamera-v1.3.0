#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import pathlib
import re
import shutil
import sys

FIXED_OTA = "http://124.221.112.55:8002/xiaozhi/ota/"
BOARD_DIR = "sp-esp32-s3-lcd-3.5-cam-ml307"
BOARD_SYMBOL = "CONFIG_BOARD_TYPE_SPOTPEAR_ESP32_S3_3_5_LCD_CAM_ML307=y"


def die(msg: str) -> None:
    raise SystemExit(f"[patch:error] {msg}")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        die(f"{label}: expected exactly 1 match, found {count}")
    return text.replace(old, new, 1)


def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> None:
    if len(sys.argv) != 2:
        die("usage: apply_customizations.py <xiaozhi-esp32-dir>")

    root = pathlib.Path(sys.argv[1]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[1]
    if not (root / "main" / "application.cc").is_file():
        die(f"not a xiaozhi source tree: {root}")

    board = root / "main" / "boards" / BOARD_DIR / f"{BOARD_DIR}.cc"
    board_json = root / "main" / "boards" / BOARD_DIR / "config.json"
    ota = root / "main" / "ota.cc"
    cmake = root / "main" / "CMakeLists.txt"
    sdkconfig = root / "sdkconfig"
    defaults = root / "sdkconfig.defaults"
    if not board.is_file():
        die(f"factory MAX35 4G board not found: {board}")

    # Copy AURORA display implementation into the factory source tree.
    for name in ("aurora_max35_display.h", "aurora_max35_display.cc"):
        src = repo_root / "overlay" / name
        dst = root / "main" / "display" / name
        if not src.is_file():
            die(f"missing overlay file: {src}")
        shutil.copy2(src, dst)
        print(f"[patch] copied {name}")

    # Add the display source to the factory component build list.
    text = cmake.read_text(encoding="utf-8")
    text = replace_once(
        text,
        '            "display/lcd_display.cc"\n',
        '            "display/lcd_display.cc"\n            "display/aurora_max35_display.cc"\n',
        "main/CMakeLists.txt AURORA source registration",
    )
    cmake.write_text(text, encoding="utf-8")

    # Patch ONLY this MAX35 4G board: keep the factory LCD/ML307/audio/touch/power code,
    # swap the display object, and skip camera startup entirely for this debug/stable build.
    text = board.read_text(encoding="utf-8")
    text = replace_once(
        text,
        '#include "display/lcd_display.h"\n',
        '#include "display/lcd_display.h"\n#include "display/aurora_max35_display.h"\n',
        "board display include",
    )
    text = replace_once(
        text,
        '        display_ = new SpiLcdDisplay(panel_io, panel,\n'
        '                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);',
        '        display_ = new AuroraMax35Display(panel_io, panel,\n'
        '                                          DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);',
        "board display construction",
    )

    # Remove the factory camera-error log monitor entirely. It forces a reboot when camera
    # sensor probing fails; this NoCamera build must never install or retain that hook.
    monitor_block = re.compile(
        r"// ================= 日志监控与拦截逻辑 =================.*?(?=typedef struct \{)",
        re.S,
    )
    text, n = monitor_block.subn(
        "// AURORA NoCamera: factory camera-failure reboot monitor removed.\n\n",
        text,
        count=1,
    )
    if n != 1:
        die(f"camera reboot-monitor block: expected 1 match, found {n}")

    monitor_install = re.compile(
        r"\n\s*if \(default_vprintf == NULL\) \{\s*\n"
        r"\s*default_vprintf = esp_log_set_vprintf\(camera_log_monitor_vprintf\);\s*\n"
        r"\s*\}\s*\n",
        re.MULTILINE,
    )
    text, n = monitor_install.subn(
        "\n        // AURORA NoCamera: camera-failure reboot hook is intentionally absent.\n",
        text,
        count=1,
    )
    if n != 1:
        die(f"camera reboot-monitor install: expected 1 match, found {n}")

    text = replace_once(
        text,
        '        InitializeCamera();\n',
        '        // AURORA NoCamera: camera startup intentionally skipped.\n',
        "InitializeCamera call",
    )
    text = replace_once(
        text,
        '    virtual Camera* GetCamera() override {\n        return camera_;\n    }',
        '    virtual Camera* GetCamera() override {\n        return nullptr;\n    }',
        "GetCamera override",
    )
    board.write_text(text, encoding="utf-8")

    # Hard-lock the OTA/backend discovery URL in source; do not allow old NVS wifi/ota_url to override it.
    text = ota.read_text(encoding="utf-8")
    pattern = re.compile(
        r"std::string Ota::GetCheckVersionUrl\(\) \{\s*\n"
        r"\s*Settings settings\(\"wifi\", false\);\s*\n"
        r"\s*std::string url = settings\.GetString\(\"ota_url\"\);\s*\n"
        r"\s*if \(url\.empty\(\)\) \{\s*\n"
        r"\s*url = CONFIG_OTA_URL;\s*\n"
        r"\s*\}\s*\n"
        r"\s*return url;\s*\n"
        r"\s*\}",
        re.MULTILINE,
    )
    replacement = (
        'std::string Ota::GetCheckVersionUrl() {\n'
        '    // AURORA 4G: this device has no Wi-Fi provisioning path, so the backend\n'
        '    // discovery/OTA endpoint is fixed at build time and cannot be overridden by NVS.\n'
        '    return CONFIG_OTA_URL;\n'
        '}'
    )
    text, n = pattern.subn(replacement, text, count=1)
    if n != 1:
        die(f"Ota::GetCheckVersionUrl: expected 1 factory implementation, found {n}")
    ota.write_text(text, encoding="utf-8")

    # Exact factory sdkconfig is kept as the hardware baseline; only replace OTA URL.
    text = sdkconfig.read_text(encoding="utf-8")
    text, n = re.subn(r'^CONFIG_OTA_URL="[^"]*"$', f'CONFIG_OTA_URL="{FIXED_OTA}"', text, count=1, flags=re.MULTILINE)
    if n != 1:
        die(f"sdkconfig CONFIG_OTA_URL: expected 1 match, found {n}")
    if BOARD_SYMBOL not in text:
        die(f"factory board symbol missing from sdkconfig: {BOARD_SYMBOL}")
    sdkconfig.write_text(text, encoding="utf-8")

    # Also add the URL to defaults as a fallback if a future clean configure regenerates sdkconfig.
    defaults_text = defaults.read_text(encoding="utf-8") if defaults.is_file() else ""
    defaults_text = re.sub(r'^CONFIG_OTA_URL="[^"]*"\s*$', '', defaults_text, flags=re.MULTILINE).rstrip() + "\n"
    defaults_text += f'CONFIG_OTA_URL="{FIXED_OTA}"\n'
    defaults.write_text(defaults_text, encoding="utf-8")

    # Put the fixed URL in the exact factory release variant too. release.py performs
    # `idf.py set-target`, then appends this list, so this is the final build-time guard.
    data = json.loads(board_json.read_text(encoding="utf-8"))
    matched_build = False
    for build in data.get("builds", []):
        if build.get("name") != BOARD_DIR:
            continue
        matched_build = True
        items = [x for x in build.get("sdkconfig_append", []) if not x.startswith("CONFIG_OTA_URL=")]
        items.append(f'CONFIG_OTA_URL="{FIXED_OTA}"')
        build["sdkconfig_append"] = items
    if not matched_build:
        die(f"factory build variant missing from {board_json}")
    board_json.write_text(json.dumps(data, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")

    print(f"[patch] factory board : {BOARD_DIR}")
    print(f"[patch] fixed OTA     : {FIXED_OTA}")
    print("[patch] camera        : startup disabled / GetCamera() returns nullptr")
    print("[patch] UI            : AURORA home + DIALOG + long-answer auto-scroll")
    print(f"[patch] board sha256  : {sha256(board)}")
    print("[patch] done")


if __name__ == "__main__":
    main()
