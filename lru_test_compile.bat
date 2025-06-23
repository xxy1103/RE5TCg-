@echo off
chcp 65001 >nul
echo ══════════════════════════════════════════════════════════
echo           LRU缓存测试程序编译脚本 v2.0
echo ══════════════════════════════════════════════════════════
echo.

echo [1] 编译LRU缓存测试程序...
echo     🔨 使用GCC编译...

gcc -I./include ^
    lru_cache_test.c ^
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
    -o lru_cache_test.exe ^
    -lws2_32 ^
    -O2

if %errorlevel% equ 0 (
    echo.
    echo     ✅ 编译成功！
    echo     📍 生成文件: lru_cache_test.exe
    
    for %%i in (lru_cache_test.exe) do (
        echo     📦 文件大小: %%~zi 字节
    )
    
    echo.
    echo     🚀 可用命令:
    echo        运行测试: .\lru_cache_test.exe
    echo        快速测试: echo 1 | .\lru_cache_test.exe
    echo.
    
    set /p choice="是否立即运行LRU测试程序? (y/n): "
    if /i "%choice%"=="y" (
        echo     🌟 正在启动LRU测试程序...
        start "LRU缓存测试" .\lru_cache_test.exe
        echo     ✅ 测试程序已在新窗口中启动
    )
) else (
    echo.
    echo     ❌ 编译失败！
    echo     请检查错误信息并修复代码问题
)

echo.
echo 按任意键退出...
pause >nul 