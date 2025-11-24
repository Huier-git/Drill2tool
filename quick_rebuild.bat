@echo off
echo ========================================
echo 快速重新编译
echo ========================================

echo 编译 MdbWorker...
nmake
if %errorlevel% neq 0 (
    echo ❌ 编译失败！
    pause
    exit /b 1
)

echo ✅ 编译成功！
echo.
echo 现在可以测试了
pause
