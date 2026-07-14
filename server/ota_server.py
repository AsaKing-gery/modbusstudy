#!/usr/bin/env python3
"""
OTA 固件升级 HTTP 服务器（本地开发/测试用）

用法:
    python ota_server.py [port]

    port: 默认 8080

服务文件:
    /version.txt       → 版本号 (纯文本，如 40501)
    /latest.txt        → 最新版本 JSON
    /firmware.bin      → 固件二进制
    /firmware.bin.sig  → HMAC-SHA256 签名 (32 bytes)

前置步骤:
    1. 复制 firmware.bin 到本目录:
       copy ..\\.pio\\build\\app\\firmware.bin server\\firmware.bin
    2. 生成签名:
       python sign.py firmware.bin firmware.bin.sig
    3. 更新 version.txt 中的版本号

注意:
    本服务器仅用于本地测试，请勿用于生产环境。
    ESP32 固件中 OTA_SERVER_HOST 需指向本机 IP。
"""

import http.server
import os
import sys
import json

# === 配置 ===
SERVER_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_PORT = 8080

# 当前最新版本号 (与 STM32 固件 FIRMWARE_VERSION 宏对应)
# 修改此文件或 version.txt 均可
VERSION_FILE = os.path.join(SERVER_DIR, "version.txt")


class OtaHandler(http.server.SimpleHTTPRequestHandler):
    """OTA 专用 HTTP 处理器"""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=SERVER_DIR, **kwargs)

    def log_message(self, format, *args):
        # 自定义日志格式
        print(f"[HTTP] {self.client_address[0]} - {format % args}")

    def do_GET(self):
        path = self.path.split("?")[0]  # 去掉查询参数

        # /latest.txt → 返回 JSON 格式版本信息
        if path == "/latest.txt":
            self._serve_latest()
            return

        # /version.txt 和其他文件 → 默认静态服务
        super().do_GET()

    def _serve_latest(self):
        """返回 JSON 版本信息"""
        try:
            with open(VERSION_FILE, "r") as f:
                version_str = f.read().strip()
        except FileNotFoundError:
            version_str = "40501"

        info = {
            "version": int(version_str),
            "version_str": f"{version_str}",
            "url": "/firmware.bin",
            "sig_url": "/firmware.bin.sig",
            "size": 0,
        }

        # 尝试获取固件大小
        fw_path = os.path.join(SERVER_DIR, "firmware.bin")
        if os.path.exists(fw_path):
            info["size"] = os.path.getsize(fw_path)

        body = json.dumps(info, indent=2).encode("utf-8")

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)


def check_files():
    """检查必要文件是否存在"""
    required = ["version.txt", "firmware.bin", "firmware.bin.sig"]
    missing = []
    for f in required:
        path = os.path.join(SERVER_DIR, f)
        if os.path.exists(path):
            size = os.path.getsize(path)
            print(f"  [OK] {f} ({size} bytes)")
        else:
            print(f"  [MISSING] {f}")
            missing.append(f)

    if missing:
        print()
        print("  缺少文件! 请执行以下步骤:")
        print(f"    cd {SERVER_DIR}")
        print(f"    copy ..\\.pio\\build\\app\\firmware.bin server\\firmware.bin")
        print(f"    python sign.py firmware.bin firmware.bin.sig")
        return False
    return True


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_PORT

    print("=" * 56)
    print("  OTA 固件升级 HTTP 服务器")
    print("=" * 56)
    print()
    print("[检查文件]")
    check_files()
    print()

    server = http.server.HTTPServer(("0.0.0.0", port), OtaHandler)
    print(f"[启动] 监听 0.0.0.0:{port}")
    print(f"  ESP32 端需设置 OTA_SERVER_HOST = 本机 IP")
    print(f"  本机测试: http://localhost:{port}/version.txt")
    print()

    # 打印本机 IP
    import socket
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        local_ip = s.getsockname()[0]
        s.close()
        print(f"  本机局域网 IP: {local_ip}")
        print(f"  ESP32 配置: OTA_SERVER_HOST = \"{local_ip}\"")
    except Exception:
        pass
    print()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[关闭] 服务器停止")


if __name__ == "__main__":
    main()
