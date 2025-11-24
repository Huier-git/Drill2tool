@echo off
echo ========================================
echo 重新编译 UI 文件和项目
echo ========================================
echo.

echo 1. 编译 UI 文件...
uic forms\SensorPage.ui -o include\ui_SensorPage.h
if %errorlevel% neq 0 (
    echo ❌ UI 编译失败！
    pause
    exit /b 1
)
echo ✅ UI 文件编译成功

echo.
echo 2. 运行 qmake...
qmake DrillControl.pro
if %errorlevel% neq 0 (
    echo ❌ qmake 失败！
    pause
    exit /b 1
)
echo ✅ qmake 成功

echo.
echo 3. 编译项目 (nmake)...
nmake
if %errorlevel% neq 0 (
    echo ❌ 编译失败！
    pause
    exit /b 1
)
echo ✅ 编译成功

echo.
echo ========================================
echo 编译完成！现在可以运行程序了
echo ========================================
pause
