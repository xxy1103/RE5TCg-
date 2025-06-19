#!/bin/bash

# 跨平台构建测试脚本
# 用于验证项目在不同平台上的编译和运行情况

echo "=== DNS跨平台兼容性测试 ==="
echo

# 检测操作系统
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    PLATFORM="Linux"
    EXECUTABLE="./bin/my_DNS"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    PLATFORM="macOS"
    EXECUTABLE="./bin/my_DNS"
elif [[ "$OSTYPE" == "cygwin" ]] || [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "win32" ]]; then
    PLATFORM="Windows"
    EXECUTABLE="./bin/my_DNS.exe"
else
    PLATFORM="Unknown"
    echo "❌ 未知的操作系统: $OSTYPE"
    exit 1
fi

echo "🔍 检测到平台: $PLATFORM"
echo

# 检查依赖
echo "🔧 检查构建依赖..."

# 检查CMake
if ! command -v cmake &> /dev/null; then
    echo "❌ CMake 未安装"
    echo "请安装CMake 3.10或更高版本"
    exit 1
fi

CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
echo "✅ CMake 版本: $CMAKE_VERSION"

# 检查C编译器
if command -v gcc &> /dev/null; then
    GCC_VERSION=$(gcc --version | head -n1)
    echo "✅ GCC: $GCC_VERSION"
    COMPILER="gcc"
elif command -v clang &> /dev/null; then
    CLANG_VERSION=$(clang --version | head -n1)
    echo "✅ Clang: $CLANG_VERSION"
    COMPILER="clang"
else
    echo "❌ 未找到C编译器 (gcc/clang)"
    exit 1
fi

echo

# 清理旧的构建
echo "🧹 清理旧的构建文件..."
if [ -d "build" ]; then
    rm -rf build
    echo "✅ 已清理build目录"
fi
echo

# 创建构建目录
echo "📁 创建构建目录..."
mkdir build
cd build

# 配置项目
echo "⚙️  配置项目..."
if cmake .. -DCMAKE_BUILD_TYPE=Debug; then
    echo "✅ 项目配置成功"
else
    echo "❌ 项目配置失败"
    exit 1
fi
echo

# 编译项目
echo "🔨 编译项目..."
if cmake --build .; then
    echo "✅ 编译成功"
else
    echo "❌ 编译失败"
    exit 1
fi
echo

# 检查可执行文件
echo "📦 检查可执行文件..."
if [ -f "$EXECUTABLE" ]; then
    echo "✅ 可执行文件存在: $EXECUTABLE"
    
    # 获取文件信息
    if command -v file &> /dev/null; then
        FILE_INFO=$(file "$EXECUTABLE")
        echo "📋 文件信息: $FILE_INFO"
    fi
    
    # 获取文件大小
    if [[ "$PLATFORM" == "Linux" ]] || [[ "$PLATFORM" == "macOS" ]]; then
        FILE_SIZE=$(du -h "$EXECUTABLE" | cut -f1)
        echo "📏 文件大小: $FILE_SIZE"
    fi
else
    echo "❌ 可执行文件不存在: $EXECUTABLE"
    exit 1
fi
echo

# 基本功能测试
echo "🧪 基本功能测试..."

# 检查是否需要管理员权限（端口53）
if [[ "$PLATFORM" == "Linux" ]] || [[ "$PLATFORM" == "macOS" ]]; then
    echo "ℹ️  注意: 在Linux/macOS上，绑定端口53可能需要root权限"
    echo "   如果测试失败，请使用: sudo $EXECUTABLE"
fi

# 简单的启动测试（timeout后终止）
echo "🚀 尝试启动DNS服务器（5秒测试）..."
if timeout 5s "$EXECUTABLE" 2>&1 | head -10; then
    echo "✅ 程序启动正常"
else
    EXIT_CODE=$?
    if [ $EXIT_CODE -eq 124 ]; then
        echo "✅ 程序启动正常（超时终止，符合预期）"
    else
        echo "⚠️  程序启动可能有问题，退出码: $EXIT_CODE"
        echo "💡 这可能是由于权限问题或端口占用"
    fi
fi
echo

# 测试总结
echo "📊 测试总结"
echo "=============="
echo "平台: $PLATFORM"
echo "编译器: $COMPILER"
echo "构建类型: Debug"
echo "可执行文件: $EXECUTABLE"
echo

# 下一步建议
echo "📝 下一步建议:"
echo "1. 如需正式运行，请确保："
echo "   - 端口53未被占用"
echo "   - 具有适当的网络权限"
if [[ "$PLATFORM" == "Linux" ]] || [[ "$PLATFORM" == "macOS" ]]; then
    echo "   - 使用 sudo 运行或配置端口权限"
fi
echo
echo "2. 如需Release版本，使用："
echo "   cmake .. -DCMAKE_BUILD_TYPE=Release"
echo
echo "3. 查看日志文件 bin/log.txt 获取详细运行信息"
echo

echo "🎉 跨平台兼容性测试完成！"
