@echo off
echo ========================================
echo 测试传感器模拟器连接
echo ========================================
echo.

echo 测试 VK701 (端口 8234)...
python -c "import socket; s=socket.socket(); s.settimeout(2); result=s.connect_ex(('127.0.0.1', 8234)); print('VK701: 连接成功' if result==0 else 'VK701: 未启动'); s.close()"

echo.
echo 测试 Modbus TCP (端口 502)...
python -c "import socket; s=socket.socket(); s.settimeout(2); result=s.connect_ex(('127.0.0.1', 502)); print('Modbus: 连接成功' if result==0 else 'Modbus: 未启动'); s.close()"

echo.
echo 测试 ZMotion (端口 8001)...
python -c "import socket; s=socket.socket(); s.settimeout(2); result=s.connect_ex(('127.0.0.1', 8001)); print('ZMotion: 连接成功' if result==0 else 'ZMotion: 未启动'); s.close()"

echo.
echo ========================================
echo 测试完成
echo ========================================
pause
