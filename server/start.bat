@echo off
REM OTA 服务器启动前准备：
REM   1. 复制 APP firmware.bin
REM   2. 生成 HMAC-SHA256 签名
REM   3. 启动 HTTP 服务器

echo ============================================================
echo   OTA 服务器启动脚本
echo ============================================================
echo.

cd /d "%~dp0"

echo [1/3] 复制 firmware.bin...
copy /Y "..\.pio\build\app\firmware.bin" "firmware.bin" >nul 2>&1
if %errorlevel% equ 0 (
    echo   [OK] firmware.bin 复制成功
) else (
    echo   [ERR] 无法复制 firmware.bin，请先编译 APP: pio run -e app
    pause
    exit /b 1
)

echo.
echo [2/3] 生成签名...
python sign.py firmware.bin firmware.bin.sig
if %errorlevel% neq 0 (
    echo   [ERR] 签名生成失败
    pause
    exit /b 1
)

echo.
echo [3/3] 启动 HTTP 服务器...
python ota_server.py %1
pause
