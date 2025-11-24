# 传感器模拟器使用说明

本目录包含3个Python脚本，用于模拟钻机采集系统的各种传感器和控制器，便于测试上位机逻辑。

## 环境要求

```bash
pip install numpy
```

## ⚠️ 重要说明

**本模拟器配置已与原项目严格对齐：**
- VK701端口：**8234**（原项目配置）
- Modbus TCP地址：**192.168.1.200:502**（原项目配置）
- ZMotion地址：**192.168.1.11**（原项目默认配置）

## 模拟器列表

### 1. VK701振动采集卡模拟器 (`vk701_simulator.py`)

**功能：** 模拟3通道振动传感器数据采集

**参数：**
- 采样率：5000Hz（可配置）
- 通道数：3（可配置）
- 数据格式：float32数组
- 通信协议：自定义TCP协议
- **默认端口：8234**（与原项目一致）

**使用方法：**
```bash
# 默认配置（监听0.0.0.0:8234）
python vk701_simulator.py

# 自定义配置
python vk701_simulator.py --host 127.0.0.1 --port 8234 --channels 3 --rate 5000
```

**数据包格式：**
```
Header (20 bytes):
  - MAGIC: 0x564B3730 (4 bytes, "VK70")
  - CHANNELS: int32 (4 bytes)
  - SAMPLES: int32 (4 bytes)
  - TIMESTAMP: int64 microseconds (8 bytes)

Data:
  - CH0_DATA: float32[SAMPLES]
  - CH1_DATA: float32[SAMPLES]
  - CH2_DATA: float32[SAMPLES]
```

**上位机配置：**
- 地址：127.0.0.1 或模拟器运行主机IP（原项目使用本地DLL，此处为模拟）
- 端口：**8234**
- Card ID：0
- 频率：5000Hz

---

### 2. Modbus TCP传感器模拟器 (`modbus_tcp_simulator.py`)

**功能：** 模拟4个力/扭矩/位移传感器

**传感器列表：**
1. 上压力传感器 (Force Upper) - 寄存器0-1
2. 下压力传感器 (Force Lower) - 寄存器2-3
3. 扭矩传感器 (Torque) - 寄存器4-5
4. 位移传感器 (Position) - 寄存器6-7

**参数：**
- 采样率：10Hz
- 数据格式：IEEE754 float32（每个占2个寄存器）
- 通信协议：标准Modbus TCP

**使用方法：**
```bash
# 默认配置（监听0.0.0.0:502）
python modbus_tcp_simulator.py

# 自定义配置
python modbus_tcp_simulator.py --host 127.0.0.1 --port 502
```

**寄存器映射：**
| 寄存器地址 | 数据类型 | 说明 | 单位 | 范围 |
|-----------|---------|------|------|------|
| 0-1 | float32 | 上压力 | N | 1000-3000 |
| 2-3 | float32 | 下压力 | N | 500-1500 |
| 4-5 | float32 | 扭矩 | N·m | 50-150 |
| 6-7 | float32 | 位移 | mm | 0-100 |

**上位机配置：**
- 地址：**192.168.1.200**（原项目配置）
- 端口：502
- 频率：10Hz

---

### 3. ZMotion运动控制器模拟器 (`zmotion_simulator.py`)

**功能：** 模拟多个电机的运动参数

**参数列表（每个电机）：**
1. 位置 (DPOS) - 度
2. 速度 (SPEED) - units/s
3. 扭矩 (TORQUE) - N·m
4. 电流 (CURRENT) - A

**参数：**
- 采样率：100Hz
- 电机数量：4（可配置）
- 通信协议：简化的文本命令协议

**使用方法：**
```bash
# 默认配置（监听0.0.0.0:8001，4个电机）
python zmotion_simulator.py

# 自定义配置
python zmotion_simulator.py --host 127.0.0.1 --port 8001 --motors 4
```

**支持的命令：**
```
GET_DPOS(axis)        - 获取电机位置，返回：OK: 123.4567
GET_SPEED(axis)       - 获取电机速度，返回：OK: 50.1234
GET_TORQUE(axis)      - 获取电机扭矩，返回：OK: 7.8900
GET_CURRENT(axis)     - 获取电机电流，返回：OK: 3.4500
GET_ALL(axis)         - 获取所有参数（JSON格式）
GET_ALL_MOTORS        - 获取所有电机所有参数（JSON格式）
```

**命令示例：**
```bash
# 使用telnet测试
telnet 127.0.0.1 8001
GET_DPOS(0)
GET_ALL(1)
GET_ALL_MOTORS
```

**上位机配置：**
- 地址：**192.168.1.11**（原项目默认配置）
- 端口：8001（模拟器使用，原项目使用ZMotion库无需指定端口）
- 频率：100Hz

---

## 同时运行多个模拟器

推荐在不同终端窗口分别启动：

**终端1 - VK701振动传感器：**
```bash
python vk701_simulator.py
```

**终端2 - Modbus TCP传感器：**
```bash
python modbus_tcp_simulator.py
```

**终端3 - ZMotion控制器：**
```bash
python zmotion_simulator.py
```

## 测试流程

1. 启动所有模拟器
2. 打开上位机 DrillControl
3. 进入"数据采集"页面
4. **配置各传感器（已设置原项目默认值）：**
   - VK701：地址=127.0.0.1（测试用），端口=**8234**，Card ID=0，频率=5000Hz
   - Modbus：地址=**192.168.1.200**（测试时改为127.0.0.1），端口=502，频率=10Hz
   - ZMotion：地址=**192.168.1.11**（测试时改为127.0.0.1），频率=100Hz
5. 点击"连接"按钮建立连接
6. 点击"开始新轮次"创建采集轮次
7. 点击"启动全部采集"开始数据采集
8. 观察状态显示和数据库写入
9. 点击"停止全部采集"停止采集
10. 点击"结束当前轮次"关闭轮次

## 注意事项

1. **IP地址配置：**
   - 默认IP地址已设置为原项目配置值
   - **本地测试时**需要将Modbus和ZMotion的IP改为127.0.0.1
   - **实际硬件连接时**使用原配置的IP地址
2. **端口配置：**
   - VK701端口**必须是8234**（与原项目一致）
   - Modbus TCP使用标准端口502
3. **端口冲突：** 确保配置的端口没有被其他程序占用
4. **防火墙：** 如果跨机器测试，确保防火墙允许相应端口通信
5. **数据格式：** 模拟器发送的数据格式应与上位机Worker解析逻辑一致
6. **性能测试：** 模拟器会持续发送数据，可以用于压力测试

## 故障排查

**问题：连接被拒绝**
- 检查模拟器是否正常运行
- 检查IP地址和端口配置是否正确
- 检查防火墙设置

**问题：数据接收不正常**
- 检查数据包格式是否与Worker解析匹配
- 查看上位机Debug输出的错误信息
- 使用wireshark抓包分析通信

**问题：高频数据丢失**
- 检查网络带宽是否足够（5000Hz振动数据量较大）
- 尝试降低采样率测试
- 检查上位机缓冲队列是否溢出

---

*Created for KT DrillControl Testing*
*Last Updated: 2025-01-24*
