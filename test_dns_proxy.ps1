# DNS代理服务器测试脚本
# 注意：必须以管理员权限运行

Write-Host "=== DNS代理服务器测试脚本 ===" -ForegroundColor Green

# 检查是否以管理员权限运行
if (-NOT ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")) {
    Write-Host "错误：此脚本需要以管理员权限运行!" -ForegroundColor Red
    Write-Host "请右键点击PowerShell图标，选择'以管理员身份运行'，然后重新执行此脚本。" -ForegroundColor Yellow
    pause
    exit 1
}

Write-Host "管理员权限验证通过" -ForegroundColor Green

# 启动DNS代理服务器（后台进程）
Write-Host "启动DNS代理服务器（端口53）..." -ForegroundColor Yellow
$process = Start-Process -FilePath ".\build\bin\my_DNS.exe" -WindowStyle Normal -PassThru

Write-Host "DNS代理服务器已启动，进程ID: $($process.Id)" -ForegroundColor Green
Write-Host "等待服务器初始化..." -ForegroundColor Yellow
Start-Sleep -Seconds 3

# 测试DNS查询
Write-Host "`n开始测试DNS查询..." -ForegroundColor Yellow

# 测试1：查询www.baidu.com
Write-Host "`n测试1: 查询 www.baidu.com" -ForegroundColor Cyan
try {
    $result1 = nslookup www.baidu.com 127.0.0.1 2>&1
    Write-Host "查询结果:" -ForegroundColor Green
    $result1 | ForEach-Object { Write-Host $_ }
} catch {
    Write-Host "查询失败: $_" -ForegroundColor Red
}

# 测试2：查询google.com
Write-Host "`n测试2: 查询 google.com" -ForegroundColor Cyan
try {
    $result2 = nslookup google.com 127.0.0.1 2>&1
    Write-Host "查询结果:" -ForegroundColor Green
    $result2 | ForEach-Object { Write-Host $_ }
} catch {
    Write-Host "查询失败: $_" -ForegroundColor Red
}

# 测试3：查询GitHub
Write-Host "`n测试3: 查询 github.com" -ForegroundColor Cyan
try {
    $result3 = nslookup github.com 127.0.0.1 2>&1
    Write-Host "查询结果:" -ForegroundColor Green
    $result3 | ForEach-Object { Write-Host $_ }
} catch {
    Write-Host "查询失败: $_" -ForegroundColor Red
}

Write-Host "`n测试完成！" -ForegroundColor Green
Write-Host "DNS代理服务器仍在运行中，检查服务器窗口查看详细日志。" -ForegroundColor Yellow
Write-Host "按任意键停止服务器..." -ForegroundColor Yellow
pause

# 停止DNS代理服务器
Write-Host "正在停止DNS代理服务器..." -ForegroundColor Yellow
try {
    Stop-Process -Id $process.Id -Force
    Write-Host "DNS代理服务器已停止" -ForegroundColor Green
} catch {
    Write-Host "停止服务器时出错: $_" -ForegroundColor Red
}
