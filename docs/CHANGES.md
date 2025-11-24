# KT_DrillControl 重构改动记录

**重构日期**: 2025-01-24  
**原项目路径**: `C:\Users\YMH\Desktop\drillControl`  
**新项目路径**: `D:\KT_DrillControl`

---

## 📋 目录

1. [概述](#概述)
2. [架构改动](#架构改动)
3. [连接验证改动](#连接验证改动)
4. [配置对比](#配置对比)
5. [测试说明](#测试说明)
6. [实地部署注意事项](#实地部署注意事项)

---

## 概述

本次重构遵循 KISS 原则，对原项目的数据采集系统进行了架构优化，主要改动包括：

- ✅ 统一的 Worker 架构（BaseWorker 基类）
- ✅ 真实的连接验证逻辑
- ✅ 跨线程安全的调用机制
- ✅ 统一的数据库写入接口
- ✅ 模块化的代码组织

**关键原则**: 所有网络配置和端口号严格与原项目保持一致。

---

## 架构改动

### 1. Worker 类层次结构

#### 原项目
- 每个传感器独立实现，代码重复
- 没有统一的基类
- 线程管理分散

#### 新架构
```
BaseWorker (抽象基类)
  ├── VibrationWorker  (VK701 振动传感器)
  ├── MdbWorker        (Modbus TCP 传感器)
  └── MotorWorker      (ZMotion 电机控制器)
```

**关键文件**:
- `include/dataACQ/BaseWorker.h`
- `src/dataACQ/BaseWorker.cpp`

**核心特性**:
```cpp
class BaseWorker : public QObject {
    Q_OBJECT
protected:
    // 子类必须实现
    virtual bool initializeHardware() = 0;
    virtual void shutdownHardware() = 0;
    virtual void runAcquisition() = 0;
    
    // 统一的启动/停止机制
    void start();
    void stop();
};
```

### 2. 数据采集管理器

**文件**: `control/AcquisitionManager.h/cpp`

**职责**:
- 管理所有 Worker 的生命周期
- 统一的启动/停止接口
- 轮次管理
- 错误处理和状态同步

**与原项目的对应关系**:
- 原项目: 各个 Page 独立管理线程
- 新架构: AcquisitionManager 集中管理

---

## 连接验证改动

### ⚠️ 关键改动：真实连接验证

#### 原项目连接逻辑
```cpp
// 原项目 ConnectionManagerPage.cpp
void connectModbusTcp() {
    m_mdbTcpPage->setConnectionParameters(ip, port);
    m_mdbTcpPage->performConnect();
    // 没有返回值检查，直接假设连接成功
}
```

#### 新项目连接逻辑
```cpp
// 新项目 SensorPage.cpp
void onMdbConnectClicked() {
    worker->setServerAddress(address);
    worker->setServerPort(port);
    
    // 在工作线程中测试连接
    bool connected = false;
    QMetaObject::invokeMethod(worker, "testConnection", 
                              Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, connected));
    
    if (connected) {
        // 连接成功
        QMessageBox::information(this, "连接成功", ...);
    } else {
        // 连接失败，显示详细错误信息
        QMessageBox::critical(this, "连接失败", ...);
    }
}
```

### 跨线程安全机制

**问题**: Worker 运行在独立线程，UI 在主线程，直接调用会导致线程冲突。

**解决方案**: 使用 `QMetaObject::invokeMethod` 进行线程安全调用。

#### 示例代码
```cpp
// ❌ 错误：直接调用（主线程调用工作线程对象）
bool connected = worker->testConnection();

// ✅ 正确：通过 Qt 元对象系统调用
bool connected = false;
QMetaObject::invokeMethod(worker, "testConnection", 
                          Qt::BlockingQueuedConnection,
                          Q_RETURN_ARG(bool, connected));
```

**关键点**:
- `Qt::BlockingQueuedConnection`: 主线程等待工作线程执行完成
- `Q_RETURN_ARG`: 接收返回值
- `Q_INVOKABLE`: 方法必须标记为可调用

---

## 配置对比

### 🔌 VK701 振动传感器

| 参数 | 原项目配置 | 新项目配置 | 文件位置 |
|------|-----------|-----------|---------|
| 服务器地址 | 192.168.1.10 | **192.168.1.10** ✅ | `forms/SensorPage.ui` |
| 端口 | **8234** | **8234** ✅ | `forms/SensorPage.ui` |
| 卡号 | 0 | 0 ✅ | `forms/SensorPage.ui` |
| 采样率 | 5000Hz | 5000Hz ✅ | `dataACQ/VibrationWorker.cpp` |
| 通道数 | 3 | 3 ✅ | `dataACQ/VibrationWorker.cpp` |

**连接方式**:
- 原项目: 使用 VK701 DLL 库（本地连接）
- 新项目: TCP Socket 连接（用于模拟器测试，实地需替换为 DLL）

**实地部署注意**:
```cpp
// 需要在 VibrationWorker::connectToCard() 中
// 将 QTcpSocket 替换为 VK701 DLL 调用
// #include "VK70xNMC_DAQ2.h"
// Server_TCPOpen(m_port);
// VK70xNMC_DAQOpen(m_cardId);
```

---

### 🔩 Modbus TCP 传感器

| 参数 | 原项目配置 | 新项目配置 | 文件位置 |
|------|-----------|-----------|---------|
| 服务器地址 | **192.168.1.200** | **192.168.1.200** ✅ | `forms/SensorPage.ui` |
| 端口 | 502 | 502 ✅ | `forms/SensorPage.ui` |
| 采样率 | 10Hz | 10Hz ✅ | `dataACQ/MdbWorker.cpp` |
| 传感器数量 | 4 (上压力/下压力/扭矩/位移) | 4 ✅ | `dataACQ/MdbWorker.cpp` |

**数据格式**:
- IEEE754 float32（每个占2个寄存器）
- 大端序（Big-endian）

**寄存器映射**:
```
地址 0-1: 上压力 (Force_Upper)
地址 2-3: 下压力 (Force_Lower)
地址 4-5: 扭矩 (Torque_MDB)
地址 6-7: 位移 (Position_MDB)
```

**连接实现**:
```cpp
// MdbWorker::connectToServer()
m_modbusClient = new QModbusTcpClient(this);
m_modbusClient->setConnectionParameter(
    QModbusDevice::NetworkAddressParameter, "192.168.1.200");
m_modbusClient->setConnectionParameter(
    QModbusDevice::NetworkPortParameter, 502);
m_modbusClient->connectDevice();
```

**验证逻辑**:
```cpp
// 读取寄存器 0-1 验证通信
QModbusDataUnit readUnit(QModbusDataUnit::HoldingRegisters, 0, 2);
QModbusReply *reply = m_modbusClient->sendReadRequest(readUnit, 1);
// 等待响应并解析
```

---

### ⚙️ ZMotion 运动控制器

| 参数 | 原项目配置 | 新项目配置 | 文件位置 |
|------|-----------|-----------|---------|
| 控制器地址 | **192.168.1.11** | **192.168.1.11** ✅ | `forms/SensorPage.ui` |
| 端口 | - (使用 ZMotion 库) | 8001 (模拟器) | `dataACQ/MotorWorker.cpp` |
| 采样率 | 100Hz | 100Hz ✅ | `dataACQ/MotorWorker.cpp` |
| 电机数量 | 4 | 4 ✅ | `dataACQ/MotorWorker.cpp` |

**读取参数**:
- 位置 (DPOS)
- 速度 (SPEED)
- 扭矩 (TORQUE)
- 电流 (CURRENT)

**连接方式**:
- 原项目: 使用 ZMotion SDK (`zmotion.h`)
- 新项目: TCP Socket 连接（用于模拟器测试）

**实地部署注意**:
```cpp
// 需要在 MotorWorker::connectToController() 中
// 将 QTcpSocket 替换为 ZMotion SDK 调用
// #include "zmotion.h"
// ZMC_HANDLE handle = ZAux_OpenEth("192.168.1.11", 2000);
```

---

## 测试说明

### 本地测试（使用模拟器）

#### 1. 启动模拟器
```bash
cd D:\KT_DrillControl\test

# 方式1: 一键启动
start_all_simulators.bat

# 方式2: 手动启动
python vk701_simulator.py
python modbus_tcp_simulator.py
python zmotion_simulator.py
```

#### 2. 修改上位机 IP
在 SensorPage 界面手动修改：
- VK701: `192.168.1.10` → `127.0.0.1`
- Modbus: `192.168.1.200` → `127.0.0.1`
- ZMotion: `192.168.1.11` → `127.0.0.1`

#### 3. 测试连接
- 点击各个"连接"按钮
- **成功**: 弹出"连接成功"提示
- **失败**: 弹出"连接失败"提示，包含检查清单

#### 4. 验证连接状态
```bash
# 检查端口是否监听
netstat -ano | findstr "8234"
netstat -ano | findstr "502"
netstat -ano | findstr "8001"
```

### 模拟器配置总结

| 模拟器 | 监听地址 | 监听端口 | 对应硬件 |
|--------|---------|---------|---------|
| vk701_simulator.py | 0.0.0.0 | **8234** | VK701 采集卡 |
| modbus_tcp_simulator.py | 0.0.0.0 | **502** | Modbus TCP 传感器 |
| zmotion_simulator.py | 0.0.0.0 | **8001** | ZMotion 控制器 |

---

## 实地部署注意事项

### ⚠️ 关键检查清单

#### 1. 网络配置
- [ ] 上位机 PC 与硬件设备在同一网段
- [ ] 确认硬件设备 IP 地址：
  - VK701: `192.168.1.10`
  - Modbus TCP: `192.168.1.200`
  - ZMotion: `192.168.1.11`
- [ ] 防火墙允许端口通信 (8234, 502)

#### 2. 硬件库依赖
VK701 和 ZMotion 需要硬件库支持：

**VK701 依赖**:
```cpp
// 需要添加的库文件
libs/VK70xNMC_DAQ2.lib

// 需要修改的代码
src/dataACQ/VibrationWorker.cpp
// 在 connectToCard() 中启用原生 DLL 调用
```

**ZMotion 依赖**:
```cpp
// 需要添加的库文件
libs/zmotion.lib

// 需要修改的代码
src/dataACQ/MotorWorker.cpp
// 在 connectToController() 中启用 ZMotion SDK 调用
```

#### 3. 连接测试步骤
1. 启动上位机程序
2. 依次点击"连接"按钮：
   - 连接 VK701
   - 连接 Modbus TCP
   - 连接 ZMotion
3. 观察控制台日志（qDebug 输出）
4. 确认连接状态：
   - **成功**: 弹出"连接成功"提示
   - **失败**: 查看错误信息，检查网络和硬件

#### 4. 常见问题排查

**问题 1: 连接超时**
- 检查网络连通性：`ping 192.168.1.200`
- 检查防火墙设置
- 确认硬件设备已启动

**问题 2: Modbus 读取错误**
- 检查寄存器地址是否正确（0-7）
- 确认设备 ID（默认为 1）
- 查看 Modbus 错误码

**问题 3: VK701 无法连接**
- 确认 VK701 DLL 已正确安装
- 检查端口 8234 是否被占用
- 验证采集卡硬件连接

**问题 4: ZMotion 无响应**
- 检查 ZMotion 控制器电源
- 确认网络连接正常
- 查看控制器状态指示灯

---

## 代码对比：连接逻辑

### 原项目 vs 新项目

#### Modbus TCP 连接

**原项目** (`mdbtcp.cpp`):
```cpp
connect(ui->btn_connect, &QPushButton::clicked, mdbworker, [=](){
    if(ui->btn_connect->text() == "Connect") {
        QString addr = ui->le_mdbIP->text();
        int port = ui->le_mdbPort->text().toInt();
        mdbworker->TCPConnect(port, addr);
        
        // ⚠️ 假设连接成功，没有验证
        if(mdbworker->connectStatus == true) {
            ui->btn_connect->setText("Disconnect");
            ui->btn_readStart->setEnabled(true);
        }
    }
});
```

**新项目** (`SensorPage.cpp`):
```cpp
void SensorPage::onMdbConnectClicked() {
    worker->setServerAddress(address);
    worker->setServerPort(port);
    
    // ✅ 真实连接验证
    bool connected = false;
    QMetaObject::invokeMethod(worker, "testConnection", 
                              Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, connected));
    
    if (connected) {
        m_mdbConnected = true;
        updateUIState();
        QMessageBox::information(this, "连接成功", ...);
    } else {
        m_mdbConnected = false;
        QMessageBox::critical(this, "连接失败", ...);
    }
}
```

**改进点**:
1. ✅ 真实的连接验证（读取寄存器测试）
2. ✅ 明确的成功/失败反馈
3. ✅ 线程安全的调用方式
4. ✅ 统一的错误处理

---

## 数据库改动

### 统一数据库架构

**文件**: `database/UnifiedDatabase.h/cpp`

**改进**:
- 统一的 sensor_data 表
- SensorType 枚举区分不同传感器
- 异步批量写入优化性能

**对比原项目**:
| 项目 | 原数据库 | 新数据库 |
|------|---------|---------|
| 表数量 | 3+ (Forcedata, Torquedata, Positiondata, 振动表) | 1 (sensor_data) |
| 数据类型 | 分散存储 | 统一 BLOB |
| 写入方式 | 直接写入 | 异步批量写入 |
| 轮次管理 | 分散在各表 | 统一 rounds 表 |

---

## 编译说明

### 编译步骤
```bash
cd D:\KT_DrillControl

# 编译 UI 文件
uic forms\SensorPage.ui -o include\ui_SensorPage.h

# 生成 Makefile
qmake DrillControl.pro

# 编译
nmake

# 或使用一键脚本
rebuild.bat
```

### 依赖库
- Qt 5.x (Core, Widgets, Network, SerialBus)
- SQLite3
- 实地部署需添加：
  - VK70xNMC_DAQ2.lib
  - zmotion.lib

---

## 测试记录

### 模拟器测试（已完成）

| 测试项 | 状态 | 备注 |
|--------|------|------|
| Modbus TCP 连接 | ✅ | 可正常连接和读取寄存器 |
| VK701 TCP 连接 | ✅ | 可正常连接 |
| ZMotion TCP 连接 | ✅ | 可正常连接和读取命令 |
| 跨线程调用 | ✅ | 使用 QMetaObject::invokeMethod |
| 连接失败提示 | ✅ | 显示详细错误信息 |

### 实地测试（待完成）

| 测试项 | 状态 | 备注 |
|--------|------|------|
| VK701 硬件连接 | ⏳ | 需替换为 DLL 调用 |
| Modbus TCP 硬件连接 | ⏳ | 网络配置验证 |
| ZMotion 硬件连接 | ⏳ | 需替换为 SDK 调用 |
| 数据采集完整流程 | ⏳ | 端到端测试 |
| 数据库写入验证 | ⏳ | 检查数据完整性 |

---

## 文件清单

### 新增文件
```
include/dataACQ/
  ├── BaseWorker.h              ✨ 新增：Worker 基类
  ├── VibrationWorker.h         ✨ 新增：VK701 Worker
  ├── MdbWorker.h               ✨ 新增：Modbus Worker
  └── MotorWorker.h             ✨ 新增：ZMotion Worker

src/dataACQ/
  ├── BaseWorker.cpp
  ├── VibrationWorker.cpp
  ├── MdbWorker.cpp
  └── MotorWorker.cpp

include/control/
  └── AcquisitionManager.h      ✨ 新增：采集管理器

src/control/
  └── AcquisitionManager.cpp

include/database/
  └── UnifiedDatabase.h         ✨ 新增：统一数据库

src/database/
  └── UnifiedDatabase.cpp

test/
  ├── vk701_simulator.py        ✨ 新增：VK701 模拟器
  ├── modbus_tcp_simulator.py   ✨ 新增：Modbus 模拟器
  ├── zmotion_simulator.py      ✨ 新增：ZMotion 模拟器
  ├── start_all_simulators.bat  ✨ 新增：一键启动脚本
  └── test_connection.bat       ✨ 新增：连接测试脚本

docs/
  └── CHANGES.md                ✨ 新增：本文档
```

### 修改文件
```
forms/SensorPage.ui             📝 修改：传感器页面 UI
src/ui/SensorPage.cpp           📝 修改：连接逻辑改为真实验证
include/ui/SensorPage.h         📝 修改：添加状态管理
```

---

## 联系人

**重构开发**: Claude AI Assistant  
**日期**: 2025-01-24  
**审核**: 待审核

---

## 附录：快速参考

### 启动命令
```bash
# 启动所有模拟器（测试用）
cd D:\KT_DrillControl\test
start_all_simulators.bat

# 测试连接
test_connection.bat

# 编译项目
cd D:\KT_DrillControl
rebuild.bat

# 快速编译
quick_rebuild.bat
```

### 重要配置
```cpp
// VK701
地址: 192.168.1.10
端口: 8234
频率: 5000Hz

// Modbus TCP
地址: 192.168.1.200
端口: 502
频率: 10Hz

// ZMotion
地址: 192.168.1.11
频率: 100Hz
```

### 日志位置
```
控制台输出 (qDebug)
数据库: db/unified.db
```

---

**文档版本**: v1.0  
**最后更新**: 2025-01-24
