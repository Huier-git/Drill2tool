# 钻机控制系统架构分析

## 文档说明

本文档详细分析钻机控制系统的整体架构，重点关注：
- 状态机实现方式
- 控制模式分类
- 线程架构与同步机制
- 架构优势与待改进点

此文档与 `MECHANISM_CONTROLLERS_GUIDE.md` 互补：后者关注单个控制器的实现细节，本文档关注系统整体架构设计。

---

## 一、控制方式总览

### 1.1 是否存在状态机？

**答：存在统一状态机，但非传统意义上的完整状态机框架。**

系统采用**基于枚举的轻量级状态机**设计：
- 所有9个机构共享统一的状态枚举 `MechanismState`
- 通过基类 `BaseMechanismController` 实现状态管理
- 各控制器根据自身特点实现状态转换逻辑

### 1.2 状态机定义

**文件位置**: `include/control/MechanismTypes.h:23`

```cpp
enum class MechanismState {
    Uninitialized,      // 未初始化
    Initializing,       // 初始化中
    Ready,              // 就绪
    Moving,             // 运动中
    Holding,            // 保持位置
    Error,              // 错误状态
    EmergencyStop       // 紧急停止 (注意：已定义但未使用)
};
```

### 1.3 状态管理基类

**文件位置**: `src/control/BaseMechanismController.cpp:79-120`

```cpp
class BaseMechanismController : public QObject {
protected:
    void setState(MechanismState newState, const QString& message = "") {
        if (m_state != newState) {
            // 记录状态转换日志
            qDebug() << mechanismCode() << "State:"
                     << stateToString(m_state) << "->"
                     << stateToString(newState);

            m_state = newState;

            // 清除错误信息（除非是Error状态）
            if (newState != MechanismState::Error) {
                m_lastError.clear();
            }

            // 发射状态变更信号
            emit stateChanged(m_state);

            // 初始化完成时发射专用信号
            if (newState == MechanismState::Ready &&
                m_previousState == MechanismState::Initializing) {
                emit initialized();
            }
        }
    }

    MechanismState state() const { return m_state; }

private:
    MechanismState m_state = MechanismState::Uninitialized;
    QString m_lastError;
};
```

**实际功能**（比简化版更丰富）：
1. 记录状态转换日志
2. 清除非Error状态的错误信息
3. 发射 `stateChanged` 信号
4. 在 `Initializing→Ready` 时发射 `initialized` 信号
5. 支持可选的消息参数

所有9个控制器均继承此基类：
- `FeedController` (`include/control/FeedController.h:23`)
- `RotationController`
- `PercussionController`
- `ClampController`
- `StorageController`
- `DockingController` (`include/control/DockingController.h:67`)
- `ArmExtensionController` (`include/control/ArmExtensionController.h:39`)
- `ArmGripController`
- `ArmRotationController`

---

## 二、通用状态转换流程

### 2.1 标准状态机流程

```
┌──────────────┐
│ Uninitialized│
└──────┬───────┘
       │ initialize()
       ▼
┌──────────────┐
│ Initializing │  ──失败──►  ┌───────┐
└──────┬───────┘             │ Error │
       │ 成功                └───┬───┘
       ▼                         │
┌──────────────┐                 │
│    Ready     │◄────────────────┘
└──────┬───────┘                 reset()
       │ startXXX()
       ▼
┌──────────────┐
│    Moving    │
└──────┬───────┘
       │ 到达目标/stop()
       ▼
┌──────────────┐
│   Holding    │
└──────────────┘
```

### 2.2 状态转换触发条件

| 当前状态 | 触发条件 | 目标状态 | 示例 |
|---------|---------|---------|------|
| Uninitialized | 调用 `initialize()` | Initializing | 系统启动时 |
| Initializing | 初始化成功 | Ready | 电机使能完成 |
| Initializing | 初始化失败 | Error | 驱动连接失败 |
| Ready | 调用运动指令 | Moving | `setTargetDepth()` |
| Moving | 到达目标位置 | Holding | 位置误差 < 容差 |
| Moving | 调用 `stop()` | Holding | 用户中止 |
| Error | 调用 `reset()` | Ready | 错误恢复 |
| 任意状态 | 异常发生 | Error | 通信断开 |

**注意**: `EmergencyStop` 状态已定义但当前代码中**未实际使用**。

---

## 三、控制模式分类

系统根据机构特性采用四种控制模式：

### 3.1 位置控制模式 (Position Control)

**适用机构**: Fz (进给), Sr (存储), Me (机械手伸缩), Mr (机械手旋转)

**控制流程** (`src/control/FeedController.cpp:245-287`):
```cpp
// 典型位置控制流程 (FeedController)
bool setTargetDepth(double depthMm, double speed) {
    setState(MechanismState::Moving);

    // 1. 转换深度→脉冲
    double targetPulses = mmToPulses(depthMm);

    // 2. 发送绝对运动指令
    driver()->moveAbsolute(motorId, targetPulses);

    // 3. 启动位置监控定时器 (100ms)
    m_monitorTimer->start(100);
}

// 定时器回调
void monitorTargetPosition() {
    double currentPos = driver()->getActualPosition(motorId);  // 注意：实际使用getActualPosition
    if (|currentPos - targetPos| < tolerance) {
        setState(MechanismState::Holding);
        emit targetReached();
    }
}
```

**到位判断**:
- Fz: 位置误差 < 0.5mm
- Sr/Me: 位置误差 < 容差值
- Mr: 位置误差 < 0.5°

**文件位置**: `src/control/FeedController.cpp:211`

### 3.2 力矩控制模式 (Torque Control)

**适用机构**: Cb (下夹紧), Mg (机械手夹紧)

**主要用途**:
1. 初始化找零点（堵转检测法）
2. 夹紧/松开动作（力控制）

**控制流程（以 Cb 为例）** (`src/control/ClampController.cpp:111-151`):
```cpp
// 初始化找零点
initializeClamp() {
    setState(MechanismState::Initializing);

    // 1. 切换到力矩模式
    driver()->setAxisType(motorId, MotorMode::Torque);

    // 2. 施加反向力矩 (DAC = -50)
    driver()->setDAC(motorId, -50);

    // 3. 启动监控定时器 (200ms)
    m_initTimer->start(200);
}

// 堵转检测
void monitorInit() {
    double currentPos = driver()->getActualPosition(motorId);

    // 检查位置变化
    if (|currentPos - lastPos| < stableThreshold) {
        stableCount++;
        if (stableCount >= 5) {  // 持续 1s (5*200ms)
            // 堵转检测成功 → 找到零点
            driver()->setAxisType(motorId, MotorMode::Position);
            driver()->setActualPosition(motorId, 0);
            setState(MechanismState::Ready);
        }
    } else {
        stableCount = 0;  // 重置计数
    }
}
```

**文件位置**: `src/control/ClampController.cpp:166,185`

**夹紧动作** (`src/control/ClampController.cpp:111-151`):
```cpp
close(torque) {
    // 1. 切换到力矩模式
    setAxisType(motorId, MotorMode::Torque);
    setDAC(motorId, closeDAC);  // 100

    // 2. 使用定时器延时 1s (等待夹紧完成)
    QTimer::singleShot(1000, this, [this]() {
        // 3. 切换到位置模式锁定
        double currentPos = driver()->getActualPosition(motorId);
        setAxisType(motorId, MotorMode::Position);
        driver()->setTargetPosition(motorId, currentPos);
    });
}
```

**注意**: 系统使用 `QTimer::singleShot` 而非 `QThread::sleep`，避免阻塞事件循环。

**文件位置**: `src/control/ClampController.cpp:99`

### 3.3 速度控制模式 (Velocity Control)

**适用机构**: Pr (回转), Pi (冲击)

**控制流程（以 Pr 为例）**:
```cpp
// 启动旋转
startRotation(double rpm) {
    setState(MechanismState::Moving);

    // 1. 设置速度模式
    driver()->setAxisType(motorId, MotorMode::Velocity);

    // 2. 设置速度
    driver()->setSpeed(motorId, rpmToSpeed(rpm));

    // 3. 连续运动
    driver()->moveContinuous(motorId, rpm > 0 ? 1 : -1);
}

// 停止旋转
stopRotation() {
    driver()->stop(motorId);
    setState(MechanismState::Holding);
}
```

**文件位置**: `src/control/RotationController.cpp:75,184`

**冲击机构速度控制**:
```cpp
startPercussion(double frequency) {
    if (m_isLocked) return false;  // 必须先解锁

    setState(MechanismState::Moving);
    setAxisType(motorId, MotorMode::Velocity);

    double speed = frequency * 1000.0;  // Hz → pulses/s
    setSpeed(motorId, speed);
    moveContinuous(motorId, 1);
}
```

**文件位置**: `src/control/PercussionController.cpp:197,215`

### 3.4 Modbus 轮询控制 (Modbus Polling)

**适用机构**: Dh (对接头)

**控制流程**:
```cpp
// 伸出动作
extend() {
    setState(MechanismState::Moving);

    // 1. 写入控制寄存器 (0x0010 = 1)
    writeModbusRegister(0x0010, extendCommand);

    // 2. 启动状态轮询定时器 (100ms)
    m_pollTimer->start(100);

    // 3. 启动超时定时器 (30s)
    m_timeoutTimer->start(30000);
}

// 轮询状态
void pollStatus() {
    uint16_t status = readModbusRegister(0x0011);

    if (status == extendedStatus) {  // 1 = 已伸出
        m_pollTimer->stop();
        m_timeoutTimer->stop();
        setState(MechanismState::Holding);
        emit moveCompleted(true);
    }
}
```

**寄存器定义**:
| 寄存器 | 地址 | 用途 |
|--------|------|------|
| 控制寄存器 | 0x0010 | 写入命令 (1=伸出, 2=收回, 0=停止) |
| 状态寄存器 | 0x0011 | 读取状态 (1=已伸出, 2=已收回, 3=运动中) |
| 位置寄存器 | 0x0012 | 读取位置 |

**文件位置**: `src/control/DockingController.cpp:278,431,461`

---

## 四、特殊状态机

### 4.1 冲击机构解锁/锁定状态机

**特性**: Pi (冲击机构) 独有的二级状态管理

**状态定义**:
```cpp
class PercussionController {
private:
    bool m_isLocked = true;  // 锁定状态标志
};
```

**状态转换图**:
```
    ┌─────────────────┐
    │  Ready (Locked) │
    └────────┬────────┘
             │ unlock()
             ▼
    ┌─────────────────┐
    │   Initializing  │ (力矩模式，施加反向力矩)
    │   (Unlocking)   │
    └────────┬────────┘
             │ 位置稳定 3s
             ▼
    ┌─────────────────┐
    │ Ready (Unlocked)│ (切换到位置模式锁定)
    └────────┬────────┘
             │ startPercussion()
             ▼
    ┌─────────────────┐
    │     Moving      │ (速度模式，连续运动)
    │  (Percussing)   │
    └────────┬────────┘
             │ stopPercussion() / lock()
             ▼
    ┌─────────────────┐
    │  Ready (Locked) │ (切换到位置模式)
    └─────────────────┘
```

**解锁流程详细说明**:
```cpp
unlock() {
    if (!m_isLocked) return true;  // 已解锁

    setState(MechanismState::Initializing);

    // 1. 切换到力矩模式
    setAxisType(motorId, MotorMode::Torque);

    // 2. 施加反向力矩 (unlockDAC = -30)
    setDAC(motorId, unlockDAC);

    // 3. 启动位置监控定时器 (100ms)
    m_unlockMonitorTimer->start(100);

    // 4. 启动超时定时器 (10s)
    m_unlockTimeoutTimer->start(10000);
}

// 监控解锁进度
void monitorUnlock() {
    double currentPos = getPosition(motorId);

    // 检查位置稳定性
    if (|currentPos - lastPos| < positionTolerance) {  // 1.0 脉冲
        m_stableTime += 100;  // 累积稳定时间

        if (m_stableTime >= stableTimeRequired) {  // 3000ms
            // 解锁成功
            setAxisType(motorId, MotorMode::Position);
            setTargetPosition(motorId, currentPos);  // 锁定当前位置

            m_isLocked = false;
            setState(MechanismState::Ready);
            emit unlockCompleted(true);
        }
    } else {
        m_stableTime = 0;  // 重置计时
    }
}
```

**文件位置**: `src/control/PercussionController.cpp:286,317,347`

**锁定流程**:
```cpp
lock() {
    stopPercussion();  // 停止冲击

    // 切换到位置模式锁定当前位置
    setAxisType(motorId, MotorMode::Position);
    setTargetPosition(motorId, currentPos);

    m_isLocked = true;
    setState(MechanismState::Holding);
}
```

### 4.2 机械手伸缩找零状态机

**适用机构**: Me (机械手伸缩) 初始化

**特性**: 使用力矩模式反向驱动直到堵转，确定零点位置

**状态转换图**:
```
┌──────────────┐
│    Ready     │
└──────┬───────┘
       │ initializePosition()
       ▼
┌──────────────┐
│ Initializing │ (力矩模式，反向力矩)
└──────┬───────┘
       │ 位置稳定检测 (堵转)
       ▼
┌──────────────┐
│    Ready     │ (切换到位置模式，设为零点)
└──────────────┘
```

**找零流程** (`src/control/ArmExtensionController.cpp:208-260`):
```cpp
initializePosition() {
    setState(MechanismState::Initializing);

    // 1. 切换到力矩模式
    driver()->setAxisType(motorId, MotorMode::Torque);

    // 2. 施加回收方向力矩 (initDAC = -50)
    driver()->setDAC(motorId, initDAC);

    // 3. 启动监控定时器 (200ms)
    m_initTimer->start(200);
}

void monitorInit() {
    double currentPos = driver()->getActualPosition(motorId);

    // 检查位置稳定性（堵转检测）
    if (|currentPos - lastPos| < stableThreshold) {  // 1.0 脉冲
        stableCount++;

        if (stableCount >= 5) {  // 持续 1s (5*200ms)
            // 堵转成功 → 找到零点
            driver()->stop(motorId);
            driver()->setAxisType(motorId, MotorMode::Position);
            driver()->setActualPosition(motorId, 0);

            setState(MechanismState::Ready);
            emit targetReached();
        }
    } else {
        stableCount = 0;  // 重置计数
    }
}
```

**优势**:
- 无需外部限位开关即可找到零点
- 通过堵转检测确定机械极限位置
- 适用于线性伸缩机构的初始化

### 4.3 渐进堵转检测状态机

**适用机构**: Mg (机械手夹紧) 初始化

**特性**: 逐步增大力矩直到堵转，避免过大冲击

**流程**:
```cpp
initializeGrip() {
    setState(MechanismState::Initializing);

    // 1. 切换到力矩模式
    setAxisType(motorId, MotorMode::Torque);

    // 2. 从小力矩开始
    double currentDAC = initialDAC;  // 10
    setDAC(motorId, currentDAC);

    // 3. 启动监控定时器 (200ms)
    m_initTimer->start(200);
}

void monitorInit() {
    double currentPos = driver()->getActualPosition(motorId);

    if (|currentPos - lastPos| < stableThreshold) {
        stableCount++;

        if (stableCount >= 5) {  // 持续 1s
            // 堵转成功 → 找到零点
            setAxisType(motorId, MotorMode::Position);
            setActualPosition(motorId, 0);
            setState(MechanismState::Ready);
            return;
        }
    } else {
        // 位置仍在变化 → 增大力矩
        stableCount = 0;
        currentDAC += dacIncrement;  // 增加 5

        if (currentDAC > maxDAC) {  // 80
            // 失败：力矩过大仍未堵转
            setState(MechanismState::Error);
            return;
        }

        setDAC(motorId, currentDAC);
    }
}
```

**文件位置**: `src/control/ArmGripController.cpp:272,296`

**优势**:
- 避免初始力矩过大导致机械冲击
- 适应不同负载条件
- 更安全可靠

### 4.4 回转机构力矩模式

**适用机构**: Pr (回转机构)

**特性**: 除了标准的速度控制外，还支持力矩控制模式用于特殊场景

**双模式切换**:
```
┌──────────────┐
│    Ready     │
└──────┬───────┘
       │
       ├─── startRotation(rpm) ──► Velocity Mode (速度控制)
       │
       └─── setTorque(dac) ──────► Torque Mode (力矩控制)
```

**力矩模式使用场景**:
1. 需要恒定扭矩输出
2. 防止钻杆卡死时过载
3. 堵转检测和保护

**力矩模式实现**:
```cpp
bool setTorque(double dac) {
    // 切换到力矩模式
    driver()->setAxisType(motorId, MotorMode::Torque);
    driver()->setDAC(motorId, dac);

    m_isTorqueMode = true;
    setState(MechanismState::Moving);

    return true;
}

bool stopTorque() {
    driver()->stop(motorId);
    m_isTorqueMode = false;
    setState(MechanismState::Holding);

    return true;
}
```

**力矩模式堵转检测**:
- 监控实际速度是否接近零
- 如果速度持续低于阈值，判定为堵转
- 触发保护机制或报警

**注意**: 虽然文档中力矩控制主要关注Cb/Mg夹紧机构，但Pr回转机构同样具备完整的力矩控制能力，是系统灵活性的重要体现。

---

## 五、线程架构与同步机制

### 5.1 线程职责划分

**文件位置**: `src/control/AcquisitionManager.cpp:46,89,117`

| 线程 | QThread 对象 | Worker 类 | 用途 | 频率 | 互斥保护 |
|------|-------------|-----------|------|------|---------|
| **主线程** | - | - | UI + 运动控制 | 事件驱动 | `g_mutex` |
| VibrationThread | `m_vibrationThread` | `VibrationWorker` | VK701震动采集 | 5000Hz | 独立采集 |
| MdbThread | `m_mdbThread` | `MdbWorker` | Modbus传感器采集 | 10Hz | 独立采集 |
| MotorThread | `m_motorThread` | `MotorWorker` | 电机参数采集 | 100Hz | **g_mutex (只读)** |
| DbThread | `m_dbThread` | `DbWriter` | 数据库异步写入 | 批量 | 独立写入 |

**线程创建流程**:
```cpp
// AcquisitionManager 构造
AcquisitionManager::AcquisitionManager() {
    // 1. 创建 worker 对象
    m_vibrationWorker = new VibrationWorker();
    m_mdbWorker = new MdbWorker();
    m_motorWorker = new MotorWorker();
    m_dbWriter = new DbWriter();  // 注意：是DbWriter而非DataWriter

    // 2. 创建 QThread 对象
    m_vibrationThread = new QThread(this);
    m_mdbThread = new QThread(this);
    m_motorThread = new QThread(this);
    m_dbThread = new QThread(this);

    // 3. 移动 worker 到线程
    m_vibrationWorker->moveToThread(m_vibrationThread);
    m_mdbWorker->moveToThread(m_mdbThread);
    m_motorWorker->moveToThread(m_motorThread);
    m_dbWriter->moveToThread(m_dbThread);

    // 4. 启动线程
    m_vibrationThread->start();
    m_mdbThread->start();
    m_motorThread->start();
    m_dbThread->start();
}
```

### 5.2 单句柄架构

**全局变量定义** (`src/Global.cpp:4,7`):
```cpp
ZMC_HANDLE g_handle = nullptr;  // 全局 ZMotion 句柄
QMutex g_mutex;                  // 保护 ZAux_* 调用
int MotorMap[10];                // EtherCAT 电机映射
```

**设计理念**:
- **单一句柄**: 系统仅创建一个 ZMotion 控制器连接
- **互斥保护**: 所有 `ZAux_*` API 调用必须持有 `g_mutex`
- **线程安全**: 通过 `QMutexLocker` 实现 RAII 自动解锁

### 5.3 互斥锁使用模式

**运动驱动层** (`src/control/ZMotionDriver.cpp:329-343`):
```cpp
int ZMotionDriver::moveAbsolute(int axisId, double position) {
    QMutexLocker locker(&g_mutex);  // RAII 自动解锁

    if (!g_handle) return -1;

    // 实际签名：ZAux_Direct_Single_MoveAbs(handle, axis, position)
    return ZAux_Direct_Single_MoveAbs(g_handle, axisId, position);
}

double ZMotionDriver::getActualPosition(int axisId) const {
    QMutexLocker locker(&g_mutex);

    if (!g_handle) return 0.0;

    float position;
    ZAux_Direct_GetMpos(g_handle, axisId, &position);
    return position;
}
```

**注意**: 速度和加速度通过其他方法预先设置，`moveAbsolute` 仅需传递 handle、axis、position 三个参数。

**数据采集线程** (`src/dataACQ/MotorWorker.cpp:183-196`):
```cpp
void MotorWorker::pollMotorData() {
    QMutexLocker locker(&g_mutex);  // 只读访问也需要锁

    if (!g_handle) return;

    for (int i = 0; i < 10; i++) {
        float pos, speed, dac;
        ZAux_Direct_GetMpos(g_handle, i, &pos);
        ZAux_Direct_GetMspeed(g_handle, i, &speed);  // 注意：使用GetMspeed而非GetMvel
        ZAux_Direct_GetDAC(g_handle, i, &dac);

        // 仅读取，不修改运动状态
    }
}
```

### 5.4 架构问题与修正建议

#### ⚠️ 问题 1: UI 直接访问未加锁

**发现位置**: `src/ui/SensorPage.cpp:202`, `src/ui/ControlPage.cpp:88`

**问题描述**:
```cpp
// SensorPage.cpp - 直接操作 g_handle (未加锁)
void SensorPage::connectController() {
    g_handle = ZAux_OpenEth("192.168.1.100", 5000);  // ❌ 未加锁
    // ...
}

// ControlPage.cpp - 直接调用 ZAux_* (未加锁)
void ControlPage::someOperation() {
    ZAux_Direct_SetAtype(g_handle, axisId, type);  // ❌ 未加锁
}
```

**影响**:
- 与 `ZMotionDriver` 和 `MotorWorker` 产生竞态条件
- 可能导致 ZMotion API 调用冲突

**修正方案**:
```cpp
// 方案1: 通过现有driver实例操作（推荐）
void SensorPage::connectController() {
    // 假设已有driver实例指针
    if (m_driver) {
        m_driver->connect("192.168.1.100", 5000);
    }
}

// 方案2: 手动加锁直接操作
void SensorPage::connectController() {
    QMutexLocker locker(&g_mutex);
    if (g_handle == nullptr) {
        g_handle = ZAux_OpenEth("192.168.1.100", 5000);
    }
}

// 方案3: 封装为静态方法
// ZMotionDriver.h
class ZMotionDriver {
public:
    static ZMotionDriver* sharedInstance();  // 可选的共享实例
    bool connect(const QString& ip, int port);
};

// ControlPage.cpp - 使用driver实例
void ControlPage::someOperation() {
    if (m_driver) {
        m_driver->setAxisType(axisId, type);  // 内部已加锁
    }
}
```

**注意**: `ZMotionDriver` 不是单例设计，需要通过已有实例或手动加锁访问 `g_handle`。

#### ⚠️ 问题 2: 运动互锁未实际启用

**发现位置**: `include/control/BaseMechanismController.h:91`, `src/control/BaseMechanismController.cpp:149`

**问题描述**:
- `MotionLockManager` 单例已实现
- `BaseMechanismController` 提供了 `requestMotionLock()` / `releaseMotionLock()` 接口
- **但所有控制器均未调用这些方法**

**当前状态**:
```cpp
// 仅定义，未使用
bool BaseMechanismController::requestMotionLock(const QString& description) {
    return MotionLockManager::instance()->requestMotion(
        MotionSource::AutoScript, description);
}

void BaseMechanismController::releaseMotionLock() {
    MotionLockManager::instance()->releaseMotion(MotionSource::AutoScript);
}
```

**影响**:
- 无法防止多个控制器同时运动
- 手动操作可能与自动流程冲突

**修正方案**:
```cpp
// 在每个运动指令前请求互锁
bool FeedController::setTargetDepth(double depthMm, double speed) {
    // 1. 请求互锁
    if (!requestMotionLock("Moving to depth " + QString::number(depthMm))) {
        return false;  // 被拒绝
    }

    // 2. 执行运动
    setState(MechanismState::Moving);
    driver()->moveAbsolute(motorId, mmToPulses(depthMm));
    m_monitorTimer->start(100);

    return true;
}

// 运动完成后释放互锁
void FeedController::monitorTargetPosition() {
    if (|currentPos - targetPos| < tolerance) {
        setState(MechanismState::Holding);
        releaseMotionLock();  // 释放
        emit targetReached();
    }
}
```

#### ⚠️ 问题 3: EmergencyStop 状态未使用

**问题描述**:
- `MechanismState::EmergencyStop` 已定义
- 但代码中无任何地方设置此状态

**修正方案**:
```cpp
// 在 BaseMechanismController 添加急停接口
void BaseMechanismController::emergencyStop() {
    // 1. 立即停止电机
    if (m_driver) {
        m_driver->emergencyStop(motorId);
    }

    // 2. 设置状态
    setState(MechanismState::EmergencyStop);

    // 3. 停止所有定时器
    stopAllTimers();
}

// 在主界面添加急停按钮
void MainWindow::onEmergencyStopClicked() {
    // 停止所有机构
    for (auto controller : m_controllers) {
        controller->emergencyStop();
    }

    // 触发运动互锁紧急停止
    MotionLockManager::instance()->emergencyStop();
}
```

---

## 六、架构优势

### 6.1 统一状态管理

✅ **优势**:
- 所有机构共享相同状态枚举，便于监控
- 基类统一处理状态变更和日志
- 状态信号可连接到 UI 实时显示

✅ **代码量优化**:
- 原 `ControlPage.cpp`: 6786 行
- 重构后单个控制器: < 500 行
- **代码减少 91%**

### 6.2 模块化设计

✅ **优势**:
- 9 个控制器独立封装，职责清晰
- 基于继承的多态设计，易于扩展
- 配置文件驱动，无需重新编译

### 6.3 灵活的控制模式

✅ **优势**:
- 支持位置/速度/力矩三种模式动态切换
- 特殊状态机满足复杂需求（解锁/堵转检测）
- 无限位开关时的创新初始化方法

### 6.4 线程分离

✅ **优势**:
- 高频采集（5000Hz 震动）不影响 UI 响应
- 数据库写入异步化，避免阻塞
- 运动控制与数据采集解耦

---

## 七、架构改进建议

### 7.1 立即修复（高优先级）

1. **UI 层 ZMotion 访问加锁**
   - 修改 `SensorPage.cpp` 和 `ControlPage.cpp`
   - 所有 `g_handle` 访问必须持有 `g_mutex`

2. **启用运动互锁**
   - 在所有控制器的运动指令中调用 `requestMotionLock()`
   - 运动完成后调用 `releaseMotionLock()`

3. **实现 EmergencyStop**
   - 添加急停接口
   - 在 UI 添加急停按钮

### 7.2 长期优化（中优先级）

1. **超时保护**
   - 目前仅 Dh (Modbus) 有超时机制
   - 建议所有位置控制添加运动超时检测

2. **状态持久化**
   - 断电重启后状态未知
   - 建议添加状态保存或重新找零流程

3. **传感器集成**
   - 目前全依赖编码器，无硬件限位保护
   - 建议添加原点开关和极限开关支持

4. **延时机制优化**
   - Cb/Mg 的夹紧动作使用 `QTimer::singleShot(1000)` 延时
   - 虽然不阻塞事件循环，但建议改为位置监控判断到位
   - 可提高响应速度和可靠性

---

## 八、总结

### 8.1 当前控制方式

**状态机**: 存在基于枚举的轻量级统一状态机，非传统状态机框架

**控制模式**:
- 位置控制: 4 个机构 (Fz, Sr, Me, Mr)
- 力矩控制: 2 个机构 (Cb, Mg)
- 速度控制: 2 个机构 (Pr, Pi)
- Modbus 轮询: 1 个机构 (Dh)

**特殊状态机**:
- 冲击机构解锁/锁定状态机
- 渐进堵转检测状态机

### 8.2 架构评价

✅ **优点**:
- 模块化程度高，代码清晰
- 统一状态管理便于监控
- 支持多种控制模式满足不同需求
- 线程分离提升性能

⚠️ **待改进**:
- UI 层直接访问 g_handle 未加锁（竞态风险）
- 运动互锁系统未实际启用（冲突风险）
- EmergencyStop 状态未使用
- 部分机构缺少超时保护

### 8.3 文档维护

本文档应在以下情况更新：
- 添加新的控制器或机构
- 修改状态机定义
- 优化线程架构
- 修复本文提到的架构问题

---

## 附录：关键文件索引

### 状态机相关
- `include/control/MechanismTypes.h:23` - MechanismState 定义
- `src/control/BaseMechanismController.cpp:79` - 状态管理基类

### 控制器实现
- `src/control/FeedController.cpp:211` - 位置控制示例
- `src/control/ClampController.cpp:166,185` - 力矩控制 + 堵转检测
- `src/control/RotationController.cpp:75,184` - 速度控制
- `src/control/DockingController.cpp:278,431,461` - Modbus 轮询
- `src/control/PercussionController.cpp:286,317,347` - 解锁状态机
- `src/control/ArmGripController.cpp:272,296` - 渐进堵转检测

### 线程与同步
- `src/control/AcquisitionManager.cpp:46,89,117` - 线程创建
- `src/Global.cpp:4,7` - 全局句柄定义
- `src/control/ZMotionDriver.cpp:28,175` - 互斥锁使用
- `src/dataACQ/MotorWorker.cpp:168` - 采集线程互斥

### 问题位置
- `src/ui/SensorPage.cpp:202` - UI 直接访问未加锁
- `src/ui/ControlPage.cpp:88` - UI 直接访问未加锁
- `include/control/BaseMechanismController.h:91` - 互锁接口未使用

---

**文档版本**: 1.0
**创建日期**: 2025-01-27
**最后更新**: 2025-01-27
