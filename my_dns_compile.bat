@echo off
chcp 65001 >nul
echo ══════════════════════════════════════════════════════════
echo           DNS中继服务器主程序构建脚本 v1.0
echo ══════════════════════════════════════════════════════════
echo.

echo [1] 检查运行中的进程...
tasklist | findstr my_DNS.exe >nul 2>&1
if %errorlevel% equ 0 (
    echo     ⚠️  发现运行中的my_DNS进程，正在停止...
    taskkill /F /IM my_DNS.exe >nul 2>&1
    timeout /t 2 /nobreak >nul
    echo     ✅ 进程已停止
) else (
    echo     ℹ️  没有运行中的my_DNS进程
)
echo.

echo [2] 选择构建方式:
echo     [1] 使用CMake+Ninja构建（推荐）
echo     [2] 使用GCC直接编译
echo     [3] 退出
echo.
set /p build_choice="请选择构建方式 (1-3): "

if "%build_choice%"=="1" goto cmake_build
if "%build_choice%"=="2" goto gcc_build
if "%build_choice%"=="3" goto exit
goto invalid_choice

:cmake_build
echo.
echo [3] 使用CMake+Ninja构建...
if not exist "build" (
    echo     📁 创建build目录...
    mkdir build
)

cd build
echo     🔧 配置CMake...
cmake .. -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release
if %errorlevel% neq 0 (
    echo     ❌ CMake配置失败！
    goto error_exit
)

echo     🔨 编译项目...
ninja
if %errorlevel% neq 0 (
    echo     ❌ 编译失败！
    goto error_exit
)

echo     📋 复制文件到根目录...
copy /Y bin\my_DNS.exe ..\my_DNS.exe >nul
cd ..

echo.
echo     ✅ CMake构建成功！
for %%i in (my_DNS.exe) do echo     📦 文件大小: %%~zi 字节
goto success

:gcc_build
echo.
echo [3] 使用GCC直接编译...
echo     🔨 编译中...

gcc -I./include ^
    src/main.c ^
    src/DNScache/relayBuild.c ^
    src/DNScache/free_stack.c ^
    src/debug/debug.c ^
    src/platform/platform.c ^
    src/websocket/datagram.c ^
    src/websocket/dnsServer.c ^
    src/websocket/upstream_config.c ^
    src/websocket/websocket.c ^
    src/Thread/thread_pool.c ^
    src/idmapping/idmapping.c ^
    -o my_DNS.exe ^
    -lws2_32 ^
    -O2 ^
    -Wall

if %errorlevel% neq 0 (
    echo     ❌ GCC编译失败！
    goto error_exit
)

echo.
echo     ✅ GCC编译成功！
for %%i in (my_DNS.exe) do echo     📦 文件大小: %%~zi 字节
goto success

:success
echo.
echo [4] 构建完成！
echo     📍 可执行文件: my_DNS.exe
echo.
echo     🚀 可用命令:
echo        启动服务器: .\my_DNS.exe
echo        后台运行:   start "DNS服务器" .\my_DNS.exe
echo        查看帮助:   .\my_DNS.exe --help
echo.

set /p run_choice="是否立即启动DNS服务器? (y/n): "
if /i "%run_choice%"=="y" (
    echo     🌟 正在启动DNS服务器...
    start "DNS中继服务器" .\my_DNS.exe
    echo     ✅ DNS服务器已在新窗口中启动
)
goto exit

:invalid_choice
echo ❌ 无效选择！
goto exit

:error_exit
echo.
echo ❌ 构建失败！请检查错误信息。
goto exit

:exit
echo.
echo 按任意键退出...
pause >nul 