# 机构控制器详细指南

## 概述

本文档详细描述了 DrillControl 系统中 9 个机构控制器的功能、初始化逻辑、运动状态机和传感器使用情况。

## 机构控制器总览

| 代号 | 名称 | 控制器类 | 控制模式 | 连接类型 | 电机ID |
|------|------|---------|----------|----------|--------|
| Fz | 进给机构 | FeedController | 位置 | EtherCAT | 2 |
| Pr | 回转机构 | RotationController | 速度/力矩 | EtherCAT | 0 |
| Pi | 冲击机构 | PercussionController | 位置/速度/力矩 | EtherCAT | 1 |
| Cb | 下夹紧 | ClampController | 力矩 | EtherCAT | 3 |
| Sr | 存储机构 | StorageController | 位置 | EtherCAT | 7 |
| Dh | 对接头 | DockingController | 位置 | Modbus TCP | - |
| Me | 机械手伸缩 | ArmExtensionController | 位置 | EtherCAT | 6 |
| Mg | 机械手夹紧 | ArmGripController | 力矩 | EtherCAT | 4 |
| Mr | 机械手旋转 | ArmRotationController | 位置 | EtherCAT | 5 |

---

## 1. FeedController (Fz) - 进给机构

### 功能描述

控制钻杆的垂直进给运动，是钻进过程中最核心的机构。

### 控制模式

- **位置模式 (Position Mode)**
- 使用绝对位置控制 (`moveAbsolute`)

### 初始化逻辑

```
initialize()
    ├── 1. 检查驱动连接
    ├── 2. 使能轴 (setAxisEnable)
    ├── 3. 设置位置模式 (ATYPE = 65)
    ├── 4. 配置运动参数 (速度/加减速)
    └── 5. 状态 → Ready
```

### 状态机

```
Uninitialized → Initializing → Ready ⟷ Moving → Holding
                                  ↓
                                Error
```

### 关键位置 (Key Positions)

| 代号 | 脉冲值 | 含义 |
|------|--------|------|
| A | 0 | 最底端 |
| B | 1,000,000 | 钻管底端对接结束 |
| C | 1,500,000 | 钻管底端对接开始 |
| D | 6,000,000 | 钻管顶端对接结束 |
| E | 7,000,000 | 钻具顶端对接结束 |
| F | 8,000,000 | 钻管顶端对接开始 |
| G | 9,000,000 | 钻具顶端对接开始 |
| H | 13,100,000 | 最顶端 (安全位置) |
| I | 2,000,000 | 搭载钻管后底部对接结束 |
| J | 11,000,000 | 搭载钻管后顶部对接开始 |

### 传感器使用

- **无外部传感器**
- 使用**电机编码器**读取位置
- **运动监控定时器** (100ms) 检测是否到达目标位置
- 到达判断：`|currentPos - targetPos| < 0.5mm`

### 安全限位

```cpp
bool checkSafetyLimits(double depthMm) const {
    return (depthMm >= minDepthMm && depthMm <= maxDepthMm);
}
```

- 最小深度: 58mm (底部)
- 最大深度: 1059mm (顶部)

---

## 2. RotationController (Pr) - 回转机构

### 功能描述

控制钻杆的旋转切削动作。

### 控制模式

- **速度模式 (Velocity Mode)** - 默认
- **力矩模式 (Torque Mode)** - 可切换

### 初始化逻辑

```
initialize()
    ├── 1. 检查驱动连接
    ├── 2. 使能轴
    ├── 3. 设置速度模式 (ATYPE = 66)
    ├── 4. 配置运动参数
    └── 5. 状态 → Ready
```

### 状态机

```
Ready ⟷ Moving (Rotating)
  ↓
Error
```

### 关键位置 (转速值)

| 代号 | 值 (rpm) | 含义 |
|------|----------|------|
| A | 0 | 不旋转 |
| B | 30 | 正向慢速 |
| C | -30 | 反向慢速 |
| D | 60 | 正向中速 |

### 传感器使用

- **无外部传感器**
- 读取**实际速度** (`getActualVelocity`) 判断旋转状态
- `isRotating = |actualVelocity| > 1.0`

### 力矩模式切换

```cpp
bool setTorque(double dac) {
    // 切换到力矩模式
    driver()->setAxisType(motorId, MotorMode::Torque);
    driver()->setDAC(motorId, dac);
    m_isTorqueMode = true;
}
```

---

## 3. PercussionController (Pi) - 冲击机构

### 功能描述

控制冲击器的冲击动作。特殊之处：**需要先解锁才能冲击**。

### 控制模式

- **位置模式** - 锁定状态
- **力矩模式** - 解锁过程
- **速度模式** - 冲击运行

### 初始化逻辑

```
initialize()
    ├── 1. 检查驱动连接
    ├── 2. 使能轴
    ├── 3. 设置位置模式 (默认锁定)
    ├── 4. 配置运动参数
    ├── 5. m_isLocked = true
    └── 6. 状态 → Ready (Locked)
```

### 解锁流程 (关键)

```
unlock()
    ├── 1. 切换到力矩模式
    ├── 2. 施加反向力矩 (unlockDAC = -30)
    ├── 3. 启动监控定时器 (100ms)
    ├── 4. 启动超时定时器 (10s)
    └── 5. 等待位置稳定

monitorUnlock() [定时器回调]
    ├── 读取当前位置
    ├── 检查位置变化 < positionTolerance (1.0)
    │   ├── 是 → stableCount++
    │   │       └── stableCount >= stableTime (3s)
    │   │           └── 解锁成功
    │   │               ├── 切换到位置模式
    │   │               ├── 锁定当前位置
    │   │               ├── m_isLocked = false
    │   │               └── 发射 unlockCompleted(true)
    │   └── 否 → 重置 stableCount, 继续监控
    └── 超时 → 解锁失败, 发射 unlockCompleted(false)
```

### 状态机

```
Ready (Locked) → Initializing (Unlocking) → Ready (Unlocked) ⟷ Moving (Percussing)
       ↑                                            ↓
       └──────────────── lock() ←───────────────────┘
```

### 关键位置 (频率值)

| 代号 | 值 (Hz) | 含义 |
|------|---------|------|
| A | 0 | 不冲击 |
| B | 5 | 标准冲击频率 |

### 传感器使用

- **无外部传感器**
- **位置监控**判断解锁状态
- 解锁判定：位置变化 < 1.0 脉冲，持续 3 秒

---

## 4. ClampController (Cb) - 下夹紧机构

### 功能描述

夹紧/松开钻杆，保持钻杆位置。

### 控制模式

- **力矩模式 (Torque Mode)** - 主要模式
- **位置模式** - 夹紧后锁定

### 初始化逻辑

```
initialize()
    ├── 1. 检查驱动连接
    ├── 2. 使能轴
    ├── 3. 设置位置模式
    ├── 4. 配置运动参数
    └── 5. 状态 → Ready (Unknown)
```

### 找零点初始化 (initializeClamp)

```
initializeClamp()
    ├── 1. 切换到力矩模式
    ├── 2. 施加反向力矩 (DAC = -50)
    ├── 3. 启动监控定时器 (200ms)
    └── 4. 等待位置稳定 (堵转检测)

monitorInit() [定时器回调]
    ├── 读取当前位置
    ├── 检查位置变化 < positionTolerance
    │   ├── 是 → stableCount++
    │   │       └── stableCount >= 5
    │   │           └── 初始化完成
    │   │               ├── 停止力矩输出
    │   │               ├── 切换到位置模式
    │   │               ├── 设为零点
    │   │               └── 状态 → Open
    │   └── 否 → 重置 stableCount
```

### 开/闭动作

```
open()
    ├── 切换到力矩模式
    ├── 施加张开力矩 (openDAC = -70)
    └── 延时1s后 → 状态 = Open

close(torque)
    ├── 切换到力矩模式
    ├── 施加夹紧力矩 (closeDAC = 100)
    └── 延时1s后
        ├── 读取当前位置
        ├── 切换到位置模式
        ├── 锁定当前位置
        └── 状态 = Closed
```

### 关键位置 (DAC值)

| 代号 | 值 | 含义 |
|------|-----|------|
| A | -70 | 完全张开 |
| B | 100 | 完全夹紧 |

### 传感器使用

- **无外部传感器**
- **堵转检测**判断到位
- 通过位置稳定性判断夹紧/张开完成

---

## 5. StorageController (Sr) - 存储机构

### 功能描述

控制钻杆存储仓的旋转，选择不同的钻杆槽位。

### 控制模式

- **位置模式 (Position Mode)**

### 初始化逻辑

```
initialize()
    ├── 1. 检查驱动连接
    ├── 2. 使能轴
    ├── 3. 设置位置模式
    ├── 4. 配置运动参数
    └── 5. 状态 → Ready
```

### 位置计算

```cpp
double positionToAngle(int position) const {
    return position * anglePerPosition;  // 51.43° per position
}

// 7个位置: 0°, 51.43°, 102.86°, 154.29°, 205.72°, 257.15°, 308.58°
```

### 关键位置 (脉冲值)

| 代号 | 脉冲值 | 位置 |
|------|--------|------|
| A | 0 | 位置 0 |
| B | 30,400 | 位置 1 |
| C | 60,800 | 位置 2 |
| D | 91,200 | 位置 3 |
| E | 121,600 | 位置 4 |
| F | 152,000 | 位置 5 |
| G | 182,400 | 位置 6 |

### 传感器使用

- **无外部传感器**
- 使用电机编码器位置

---

## 6. DockingController (Dh) - 对接头

### 功能描述

控制对接推杆的伸出/收回。**特殊：使用 Modbus TCP 通信**。

### 控制模式

- **Modbus TCP 寄存器控制**
- 非 EtherCAT 电机

### 初始化逻辑

```
initialize()
    ├── 1. 连接 Modbus 服务器 (192.168.1.201:502)
    ├── 2. 读取当前状态寄存器
    ├── 3. 解析状态
    └── 4. 状态 → Ready
```

### Modbus 寄存器配置

| 寄存器 | 地址 | 用途 |
|--------|------|------|
| 控制寄存器 | 0x0010 | 写入命令 |
| 状态寄存器 | 0x0011 | 读取状态 |
| 位置寄存器 | 0x0012 | 读取位置 |

### 命令值

| 命令 | 值 | 含义 |
|------|-----|------|
| 伸出 | 1 | extendCommand |
| 收回 | 2 | retractCommand |
| 停止 | 0 | stopCommand |

### 状态值

| 状态 | 值 | 含义 |
|------|-----|------|
| 已伸出 | 1 | extendedStatus |
| 已收回 | 2 | retractedStatus |
| 运动中 | 3 | movingStatus |

### 伸出/收回流程

```
extend()
    ├── 1. 写入控制寄存器 (1)
    ├── 2. 启动状态轮询定时器 (100ms)
    ├── 3. 启动超时定时器 (30s)
    └── 4. 等待状态变为 Extended

pollStatus() [定时器回调]
    ├── 读取状态寄存器
    ├── 解析状态
    │   └── 状态 == 目标状态
    │       └── 动作完成
    │           ├── 停止定时器
    │           └── 发射 moveCompleted(true)
    └── 超时 → 发射 moveCompleted(false)
```

### 关键位置

| 代号 | 值 | 含义 |
|------|------|------|
| A | -35,000 | 伸出位置 |
| B | 0 | 收回位置 |

### 传感器使用

- **使用 Modbus 状态寄存器**
- 通过轮询读取到位信号

---

## 7. ArmExtensionController (Me) - 机械手伸缩

### 功能描述

控制机械手的水平伸缩运动。

### 控制模式

- **位置模式 (Position Mode)** - 主要模式
- **力矩模式** - 初始化找零点

### 初始化逻辑

```
initialize()
    ├── 1. 检查驱动连接
    ├── 2. 使能轴
    ├── 3. 设置位置模式
    ├── 4. 配置运动参数
    └── 5. 状态 → Ready
```

### 找零点初始化 (initializePosition)

```
initializePosition()
    ├── 1. 切换到力矩模式
    ├── 2. 施加回收方向力矩 (initDAC = -50)
    ├── 3. 启动监控定时器 (200ms)
    └── 4. 等待堵转 (位置稳定)

monitorInit()
    ├── 检查位置变化 < stableThreshold (1.0)
    │   └── stableCount >= 5
    │       └── 初始化完成
    │           ├── 停止力矩
    │           ├── 切换到位置模式
    │           ├── 设为零点
    │           └── 发射 targetReached()
```

### 关键位置 (脉冲值)

| 代号 | 值 | 含义 |
|------|------|------|
| A | 0 | 完全收回 |
| B | 25,000 | 中间位置 |
| C | 50,000 | 完全伸出 |

### 传感器使用

- **无外部传感器**
- **堵转检测**找零点
- 位置监控

---

## 8. ArmGripController (Mg) - 机械手夹紧

### 功能描述

控制机械手夹爪的夹紧/松开。

### 控制模式

- **力矩模式 (Torque Mode)** - 主要模式
- **位置模式** - 夹紧后锁定

### 初始化逻辑

```
initialize()
    ├── 1. 检查驱动连接
    ├── 2. 使能轴
    ├── 3. 设置位置模式
    ├── 4. 配置运动参数
    └── 5. 状态 → Ready (Unknown)
```

### 找零点初始化 (initializeGrip)

**特殊：渐进力矩法**

```
initializeGrip()
    ├── 1. 切换到力矩模式
    ├── 2. 从小力矩开始 (initDAC = 10)
    ├── 3. 启动监控定时器 (200ms)
    └── 4. 等待堵转

monitorInit()
    ├── 检查位置变化 < stableThreshold
    │   ├── 稳定 → stableCount++
    │   │       └── stableCount >= 5
    │   │           └── 初始化完成
    │   └── 不稳定 →
    │       ├── 重置 stableCount
    │       └── 增大力矩 (currentDAC += dacIncrement)
    │           └── 直到 maxDAC (80)
```

### 开/闭动作

```
open()
    ├── 切换到力矩模式
    ├── 施加张开力矩 (openDAC = -100)
    └── 延时1s → 状态 = Open

close(torque)
    ├── 切换到力矩模式
    ├── 施加夹紧力矩 (closeDAC = 100)
    └── 延时1s
        ├── 切换到位置模式
        ├── 锁定当前位置
        └── 状态 = Closed
```

### 关键位置 (DAC值)

| 代号 | 值 | 含义 |
|------|------|------|
| A | -100 | 完全张开 |
| B | 100 | 完全夹紧 |

### 传感器使用

- **无外部传感器**
- **渐进堵转检测**找零点
- 力矩控制+位置锁定

---

## 9. ArmRotationController (Mr) - 机械手旋转

### 功能描述

控制机械手在钻进位和料仓位之间旋转。

### 控制模式

- **位置模式 (Position Mode)**

### 初始化逻辑

```
initialize()
    ├── 1. 检查驱动连接
    ├── 2. 使能轴
    ├── 3. 设置位置模式
    ├── 4. 配置运动参数
    ├── 5. 判断当前位置 (Drill/Storage/Unknown)
    └── 6. 状态 → Ready
```

### 位置判断

```cpp
ArmPosition determinePosition(double angle) const {
    if (|angle - drillPositionAngle| <= tolerance)
        return ArmPosition::Drill;
    if (|angle - storagePositionAngle| <= tolerance)
        return ArmPosition::Storage;
    return ArmPosition::Unknown;
}
```

### 预设位置

| 位置 | 角度 | 脉冲值 |
|------|------|--------|
| 钻进位 (Drill) | 0° | 0 |
| 料仓位 (Storage) | 360° | -52,000 |

### 关键位置

| 代号 | 脉冲值 | 含义 |
|------|--------|------|
| A | 0 | 钻进位 |
| B | -52,000 | 料仓位 |

### 传感器使用

- **无外部传感器**
- 使用电机编码器
- 位置容差判断到位 (0.5°)

---

## 传感器使用总结

| 机构 | 外部传感器 | 位置反馈 | 到位判断方法 |
|------|-----------|----------|--------------|
| Fz | 无 | 电机编码器 | 位置误差 < 0.5mm |
| Pr | 无 | 电机编码器 | 速度判断 |
| Pi | 无 | 电机编码器 | 堵转检测 (3s稳定) |
| Cb | 无 | 电机编码器 | 堵转检测 (5次稳定) |
| Sr | 无 | 电机编码器 | 位置控制 |
| Dh | **Modbus状态** | 状态寄存器 | 状态寄存器轮询 |
| Me | 无 | 电机编码器 | 堵转检测 (5次稳定) |
| Mg | 无 | 电机编码器 | 渐进堵转检测 |
| Mr | 无 | 电机编码器 | 位置容差 (0.5°) |

**结论**：当前系统**未使用外部限位传感器**，全部依赖电机编码器和堵转检测。唯一例外是 Dh (对接头) 使用 Modbus 状态寄存器。

---

## 状态机总览

### 通用状态 (MechanismState)

```cpp
enum class MechanismState {
    Uninitialized,      // 未初始化
    Initializing,       // 初始化中
    Ready,              // 就绪
    Moving,             // 运动中
    Holding,            // 保持位置
    Error,              // 错误状态
    EmergencyStop       // 紧急停止
};
```

### 通用状态转换图

```
                    ┌──────────────┐
                    │ Uninitialized │
                    └──────┬───────┘
                           │ initialize()
                           ▼
                    ┌──────────────┐
              ┌─────│ Initializing │─────┐
              │     └──────┬───────┘     │
              │ 失败       │ 成功        │
              ▼            ▼             │
        ┌─────────┐  ┌─────────┐        │
        │  Error  │  │  Ready  │◄───────┘
        └────┬────┘  └────┬────┘
             │            │ startXXX()
             │            ▼
             │      ┌─────────┐
             │      │ Moving  │
             │      └────┬────┘
             │           │ 到达目标/stop()
             │           ▼
             │      ┌─────────┐
             └─────►│ Holding │
                    └─────────┘
```

---

## 初始化顺序建议

在自动化流程中，建议按以下顺序初始化：

1. **Fz** (进给) - 移动到安全位置
2. **Cb** (下夹紧) - 初始化找零点
3. **Pr** (回转) - 初始化
4. **Pi** (冲击) - 初始化但保持锁定
5. **Sr** (存储) - 初始化
6. **Mr** (机械手旋转) - 初始化
7. **Me** (机械手伸缩) - 初始化找零点
8. **Mg** (机械手夹紧) - 初始化找零点
9. **Dh** (对接头) - 连接并读取状态

---

## 关键位置配置文件

配置文件路径：`config/mechanisms.json`

支持运行时热更新（通过 `MotionConfigManager`）。

### 配置示例

```json
{
  "mechanisms": {
    "Fz": {
      "name": "进给机构",
      "motor_id": 2,
      "key_positions": {
        "A": 0,
        "H": 13100000
      }
    }
  }
}
```

---

## 待改进点

1. **添加限位传感器支持**
   - 目前全依赖编码器，无硬件限位保护
   - 建议添加原点开关和极限开关

2. **运动到位确认**
   - 部分机构使用延时判断 (如 Cb, Mg 的 1s 延时)
   - 建议改为位置监控

3. **超时处理**
   - 仅 Dh (Modbus) 有完整超时处理
   - 其他机构建议添加运动超时检测

4. **状态同步**
   - 断电重启后状态未知
   - 建议添加状态持久化或重新找零流程

---

## 版本历史

| 日期 | 版本 | 变更 |
|------|------|------|
| 2025-01-26 | 1.0 | 初始文档 |
