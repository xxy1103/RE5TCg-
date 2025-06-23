@echo off
echo 正在编译LRU缓存测试程序...
gcc -I./include lru_cache_test.c src/DNScache/*.c src/debug/*.c src/platform/*.c src/websocket/*.c src/Thread/*.c src/idmapping/*.c -o lru_cache_test.exe -lws2_32
if %errorlevel% equ 0 (
    echo 编译成功！生成文件: lru_cache_test.exe
    echo 运行程序: .\lru_cache_test.exe
) else (
    echo 编译失败！
)
pause 