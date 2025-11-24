@echo off
echo ========================================
echo 启动所有传感器模拟器
echo ========================================
echo.
echo 配置信息：
echo - VK701 振动传感器: 0.0.0.0:8234
echo - Modbus TCP 传感器: 0.0.0.0:502
echo - ZMotion 控制器: 0.0.0.0:8001
echo.
echo 按 Ctrl+C 可以停止所有模拟器
echo ========================================
echo.

:: 启动 VK701 模拟器
start "VK701模拟器" cmd /k "python vk701_simulator.py"

:: 等待1秒
timeout /t 1 /nobreak >nul

:: 启动 Modbus TCP 模拟器
start "Modbus模拟器" cmd /k "python modbus_tcp_simulator.py"

:: 等待1秒
timeout /t 1 /nobreak >nul

:: 启动 ZMotion 模拟器
start "ZMotion模拟器" cmd /k "python zmotion_simulator.py"

echo.
echo 所有模拟器已启动！
echo 3个新窗口已打开，关闭窗口即可停止对应的模拟器
echo.
pause
