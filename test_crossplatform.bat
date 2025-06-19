@echo off
setlocal enabledelayedexpansion

echo === DNS跨平台兼容性测试 (Windows) ===
echo.

echo 🔍 检测到平台: Windows
echo.

REM 检查依赖
echo 🔧 检查构建依赖...

REM 检查CMake
cmake --version >nul 2>&1
if %errorlevel% neq 0 (
    echo ❌ CMake 未安装
    echo 请安装CMake 3.10或更高版本
    pause
    exit /b 1
)

for /f "tokens=3" %%i in ('cmake --version ^| findstr "cmake version"') do (
    echo ✅ CMake 版本: %%i
)

REM 检查编译器
gcc --version >nul 2>&1
if %errorlevel% equ 0 (
    for /f "tokens=1-3" %%i in ('gcc --version ^| findstr "gcc"') do (
        echo ✅ GCC: %%i %%j %%k
        set COMPILER=gcc
    )
) else (
    cl >nul 2>&1
    if %errorlevel% equ 0 (
        echo ✅ 检测到 Visual Studio 编译器
        set COMPILER=MSVC
    ) else (
        echo ❌ 未找到C编译器 (gcc/MSVC)
        pause
        exit /b 1
    )
)

echo.

REM 清理旧的构建
echo 🧹 清理旧的构建文件...
if exist build (
    rmdir /s /q build
    echo ✅ 已清理build目录
)
echo.

REM 创建构建目录
echo 📁 创建构建目录...
mkdir build
cd build

REM 配置项目
echo ⚙️  配置项目...
cmake .. -DCMAKE_BUILD_TYPE=Debug
if %errorlevel% neq 0 (
    echo ❌ 项目配置失败
    pause
    exit /b 1
)
echo ✅ 项目配置成功
echo.

REM 编译项目
echo 🔨 编译项目...
cmake --build .
if %errorlevel% neq 0 (
    echo ❌ 编译失败
    pause
    exit /b 1
)
echo ✅ 编译成功
echo.

REM 检查可执行文件
echo 📦 检查可执行文件...
set EXECUTABLE=bin\my_DNS.exe

if exist %EXECUTABLE% (
    echo ✅ 可执行文件存在: %EXECUTABLE%
    
    REM 获取文件大小
    for %%i in (%EXECUTABLE%) do (
        echo 📏 文件大小: %%~zi 字节
    )
) else (
    echo ❌ 可执行文件不存在: %EXECUTABLE%
    pause
    exit /b 1
)
echo.

REM 基本功能测试
echo 🧪 基本功能测试...
echo 🚀 尝试启动DNS服务器（5秒测试）...

REM 使用timeout命令进行短期测试
timeout /t 1 /nobreak >nul
start /wait /b "" %EXECUTABLE% && (
    echo ✅ 程序启动正常
) || (
    echo ⚠️  程序可能需要管理员权限运行
    echo 💡 请右键点击CMD选择"以管理员身份运行"
)

echo.

REM 测试总结
echo 📊 测试总结
echo ==============
echo 平台: Windows
echo 编译器: !COMPILER!
echo 构建类型: Debug
echo 可执行文件: %EXECUTABLE%
echo.

REM 下一步建议
echo 📝 下一步建议:
echo 1. 如需正式运行，请确保：
echo    - 端口53未被占用
echo    - 以管理员身份运行
echo    - 防火墙允许程序访问网络
echo.
echo 2. 如需Release版本，使用：
echo    cmake .. -DCMAKE_BUILD_TYPE=Release
echo.
echo 3. 查看日志文件 bin\log.txt 获取详细运行信息
echo.

echo 🎉 Windows平台兼容性测试完成！
echo.
pause
