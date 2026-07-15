#!/usr/bin/env python3
"""
OTA 固件签名工具 (HMAC-SHA256)

用法:
    python sign.py firmware.bin [firmware.bin.sig]

如果省略输出路径，默认 firmware.bin.sig

密钥必须与 STM32 boot_config.h 中的 HMAC_KEY 一致。
"""

import sys
import hmac
import hashlib

# === 密钥（与 STM32 src/bootloader/shared/boot_config.h 中 HMAC_KEY 一致） ===
HMAC_KEY = bytes([
    0x4B, 0x73, 0x8A, 0x1F, 0x2E, 0x6D, 0x9C, 0xB0,
    0x3F, 0x58, 0x7D, 0x12, 0x8E, 0xA4, 0x61, 0xF5,
    0x0C, 0x39, 0xD7, 0xAB, 0x45, 0x6E, 0x82, 0x19,
    0x94, 0xBF, 0xE3, 0x27, 0x50, 0x68, 0xDC, 0xFF,
])


def sign_file(fw_path: str, sig_path: str) -> bool:
    """对固件文件执行 HMAC-SHA256 签名"""
    try:
        with open(fw_path, "rb") as f:
            firmware = f.read()
    except FileNotFoundError:
        print(f"[ERR] 文件不存在: {fw_path}")
        return False

    print(f"[INFO] 固件大小: {len(firmware)} bytes ({len(firmware)/1024:.1f} KB)")

    # HMAC-SHA256
    h = hmac.new(HMAC_KEY, firmware, hashlib.sha256)
    signature = h.digest()

    with open(sig_path, "wb") as f:
        f.write(signature.hex().upper().encode())  # 64-char HEX for ESP32 parser

    print(f"[OK] 签名文件已生成: {sig_path}")
    print(f"[INFO] HMAC-SHA256: {signature.hex().upper()}")
    return True


def main():
    if len(sys.argv) < 2:
        print("用法: python sign.py firmware.bin [output.sig]")
        sys.exit(1)

    fw_path = sys.argv[1]
    sig_path = sys.argv[2] if len(sys.argv) > 2 else fw_path + ".sig"
    sign_file(fw_path, sig_path)


if __name__ == "__main__":
    main()
