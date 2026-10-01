#!/usr/bin/env python3
from __future__ import annotations

import json
import pathlib
import re
import sys

FIXED_OTA = "http://124.221.112.55:8002/xiaozhi/ota/"
BOARD_DIR = "sp-esp32-s3-lcd-3.5-cam-ml307"
BOARD_SYMBOL = "CONFIG_BOARD_TYPE_SPOTPEAR_ESP32_S3_3_5_LCD_CAM_ML307=y"


def check(cond: bool, msg: str) -> None:
    if not cond:
        raise SystemExit(f"[preflight:error] {msg}")
    print(f"[OK] {msg}")


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: preflight.py <xiaozhi-esp32-dir>")
    root = pathlib.Path(sys.argv[1]).resolve()

    board = root / "main" / "boards" / BOARD_DIR / f"{BOARD_DIR}.cc"
    config = root / "main" / "boards" / BOARD_DIR / "config.h"
    board_json = root / "main" / "boards" / BOARD_DIR / "config.json"
    ota = root / "main" / "ota.cc"
    cmake = root / "main" / "CMakeLists.txt"
    sdk = root / "sdkconfig"
    ui_h = root / "main" / "display" / "aurora_max35_display.h"
    ui_cc = root / "main" / "display" / "aurora_max35_display.cc"

    for p in (board, config, board_json, ota, cmake, sdk, ui_h, ui_cc):
        check(p.is_file(), f"exists: {p.relative_to(root)}")

    b = board.read_text(encoding="utf-8")
    o = ota.read_text(encoding="utf-8")
    c = cmake.read_text(encoding="utf-8")
    s = sdk.read_text(encoding="utf-8")
    u = ui_cc.read_text(encoding="utf-8")
    cfg = config.read_text(encoding="utf-8")

    check(BOARD_SYMBOL in s, "exact SpotPear MAX35 ML307 board selected")
    check('#define DISPLAY_WIDTH           480' in cfg and '#define DISPLAY_HEIGHT          320' in cfg, "factory 480x320 display geometry preserved")
    check('Ml307Board(ML307_TX_PIN, ML307_RX_PIN)' in b, "factory ML307 board wiring preserved")
    check('new AuroraMax35Display' in b, "AURORA display attached to the factory LCD path")
    check('InitializeCamera();' not in re.search(r'CustomBoard\(\).*?virtual AudioCodec\*', b, flags=re.S).group(0), "camera startup call removed from board constructor")
    check('return nullptr;' in re.search(r'virtual Camera\* GetCamera\(\) override \{.*?\}', b, flags=re.S).group(0), "GetCamera() disabled")
    check('esp_log_set_vprintf(camera_log_monitor_vprintf)' not in re.search(r'CustomBoard\(\).*?virtual AudioCodec\*', b, flags=re.S).group(0), "camera-failure reboot hook not installed")
    check('display/aurora_max35_display.cc' in c, "AURORA source registered with CMake")
    check(f'CONFIG_OTA_URL="{FIXED_OTA}"' in s, "sdkconfig contains fixed OTA/backend URL")
    cfg_json = json.loads(board_json.read_text(encoding="utf-8"))
    target_build = next((x for x in cfg_json.get("builds", []) if x.get("name") == BOARD_DIR), None)
    check(target_build is not None and f'CONFIG_OTA_URL="{FIXED_OTA}"' in target_build.get("sdkconfig_append", []), "release variant appends fixed OTA/backend URL after set-target")
    check('return CONFIG_OTA_URL;' in o and 'settings.GetString("ota_url")' not in re.search(r'std::string Ota::GetCheckVersionUrl\(\).*?\n\}', o, flags=re.S).group(0), "NVS OTA override disabled")
    check('ShowDialogLocked();' in u and 'kDeviceStateListening' in u, "listening/speaking automatically opens DIALOG page")
    check('assistant_text_ += "\\n";' in u, "multi-sentence assistant replies accumulate")
    check('lv_obj_scroll_to_y' in u and 'kScrollPixelsPerTick' in u, "long-answer slow auto-scroll enabled")
    check('pending_home_' in u and 'scroll_complete_' in u, "home return waits for readable scroll completion")

    print("[done] MAX35 AURORA Factory4G NoCamera preflight passed")


if __name__ == "__main__":
    main()
