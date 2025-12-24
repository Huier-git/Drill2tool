# ControlPage坐标系统与运动命令详解

**作者**: DrillControl开发团队
**日期**: 2025-12-24
**版本**: v1.0
**重要性**: ⚠️ **安全关键文档** - 错误理解可能导致电机失控

---

## 文档目的

本文档详细说明ControlPage（运动控制页面）中的坐标转换系统和运动命令使用，明确"脉冲"与"driver值"的关系，以及绝对/增量运动命令的区别。

**目标读者**: 系统开发者、现场调试工程师、安全审查人员

---

## 核心概念总结

### 关键结论

1. **"脉冲"和"driver值"是同一个东西** - 都是指ZMotion控制器使用的原始脉冲计数
2. **ControlPage使用两套坐标转换系统**：
   - **FeedController**: 专用于进给轴Fz，脉冲 ↔ mm（局部绝对坐标系）
   - **UnitConverter**: 通用转换器，driver值 ↔ 物理单位（mm/min, deg/min等）
3. **两种运动命令**：
   - **绝对命令** (`MoveAbs`) - 移动到绝对脉冲位置
   - **增量命令** (`Move`) - 从当前位置偏移指定脉冲数

---

## 零点处理机制 ⚠️ 安全关键

### 电机零点策略

```
所有电机（除推杆Dh）：上电位置即为零点
```

**含义**:
- 电机上电后，当前位置被视为**零点**（0脉冲）
- 后续所有运动均相对于此零点计算
- **没有外部限位开关** - 完全依赖编码器反馈

**安全影响**:
- 若电机在非安全位置上电，零点可能位于危险区域
- 必须在上电后执行回零流程（多数控制器使用stall detection）
- **绝对运动模式**下，目标位置相对于上电零点

---

## 坐标转换系统

### 系统1: UnitConverter（通用转换器）

**文件**: `src/control/UnitConverter.cpp`

#### 转换公式

```cpp
// 位置转换
physical_mm = driver_pulses / pulsesPerUnit

// 速度转换（单位：mm/min或deg/min）
physical_speed = (driver_pulses / pulsesPerUnit) * 60.0

// 加速度转换（单位：mm/min²或deg/min²）
physical_acc = (driver_pulses / pulsesPerUnit) * 3600.0
```

**反向转换**:
```cpp
driver_pulses = physical_mm * pulsesPerUnit
driver_pulses = (physical_speed / 60.0) * pulsesPerUnit
driver_pulses = (physical_acc / 3600.0) * pulsesPerUnit
```

#### 配置来源

1. **优先级1**: `config/unit_conversions.csv`（CSV配置文件）
2. **优先级2**: `config/mechanisms.json`（JSON配置文件）

**CSV示例** (`config/unit_conversions.csv`):
```csv
code,motor_index,unit_label,pulses_per_unit
Fz,2,mm,13086.9
Pr,0,deg,36352.5
Me,6,mm,13086.9
```

**AxisUnitInfo结构**:
```cpp
struct AxisUnitInfo {
    QString code;              // 机构代号（如"Fz"）
    int motorIndex;            // 电机索引（如2）
    QString unitLabel;         // 单位标签（如"mm"）
    double pulsesPerUnit;      // 脉冲当量（如13086.9脉冲/mm）
};
```

#### 使用示例

```cpp
// 读取电机位置（ZMotion API返回脉冲值）
double mPosDriver = 131000.0;  // 脉冲值（driver值）

// 转换为物理单位显示
AxisUnitInfo info;  // pulsesPerUnit = 13086.9
double mPosPhysical = UnitConverter::driverToPhysical(mPosDriver, info, UnitValueType::Position);
// mPosPhysical = 131000.0 / 13086.9 = 10.01 mm

// 用户输入100mm，转换为脉冲值发送给控制器
double inputMm = 100.0;
double targetDriver = UnitConverter::physicalToDriver(inputMm, info, UnitValueType::Position);
// targetDriver = 100.0 * 13086.9 = 1308690.0 脉冲
```

---

### 系统2: FeedController（进给轴专用）

**文件**: `src/control/FeedController.cpp`

#### 转换公式（新系统 - 简单线性坐标系）

```cpp
// mm → 脉冲
pulses = (mm - zeroOffsetMm) * pulsesPerMm

// 脉冲 → mm
mm = (pulses / pulsesPerMm) + zeroOffsetMm
```

**参数**:
- `pulsesPerMm` = 13086.9（从mechanisms.json读取）
- `zeroOffsetMm` = 0.0（默认值，可通过setZeroOffset修改）

#### 局部绝对坐标系定义

```
      ▲ +mm方向
      │
H (1001mm) ─────┐
               │
K (917mm)  ─────┤  土壤表面
               │
      ⋮        │  钻进区域
      ⋮        │
A (0mm)    ─────┘  机构底部（零点）
```

- **原点**: A位置（进给机构底部）= 0mm = 0脉冲
- **正方向**: 向上（朝向H位置）
- **最大值**: H位置（进给机构顶部）= 1001mm = 13100000脉冲

#### 为什么需要FeedController？

FeedController提供了**局部绝对坐标系**，与电机上电零点无关：
- UnitConverter：依赖电机上电零点
- FeedController：定义了一套机械结构的绝对坐标系（A/H/K等关键位置）

**使用场景**:
- AutoTaskPage：使用FeedController的位置引用（`@H`, `@K`, `@A`）
- DrillControlPage：使用FeedController的深度控制方法

---

## ControlPage的双系统协作

### ControlPage的封装方法

**文件**: `src/ui/ControlPage.cpp`

```cpp
// 将driver值（脉冲）转换为显示值（物理单位或脉冲）
double ControlPage::displayValueFromDriver(double driverValue, int axisIndex, UnitValueType type) const
{
    if (!m_displayPhysicalUnits) {
        return driverValue;  // 显示原始脉冲值
    }
    AxisUnitInfo info = axisUnitInfo(axisIndex);
    return UnitConverter::driverToPhysical(driverValue, info, type);  // 转换为mm/deg
}

// 将显示值（物理单位或脉冲）转换为driver值（脉冲）
double ControlPage::driverValueFromDisplay(double displayValue, int axisIndex, UnitValueType type) const
{
    if (!m_displayPhysicalUnits) {
        return displayValue;  // 直接使用脉冲值
    }
    AxisUnitInfo info = axisUnitInfo(axisIndex);
    return UnitConverter::physicalToDriver(displayValue, info, type);  // 转换为脉冲
}
```

### 显示模式切换

ControlPage支持两种显示模式（通过checkbox `cb_motor_pos_abs` 控制）:

| 显示模式 | MPos列显示 | Pos列输入 | 说明 |
|---------|-----------|----------|------|
| **物理单位模式** (`m_displayPhysicalUnits=true`) | 10.01 mm | 输入100mm | 用户友好，直观 |
| **Driver值模式** (`m_displayPhysicalUnits=false`) | 131000 脉冲 | 输入131000脉冲 | 调试用，精确 |

**状态栏显示**:
```
Driver值: 131000 脉冲  |  物理单位: 10.01 mm
```

---

## 运动命令详解 ⚠️ 安全关键

### ZMotion API读取命令

**文件**: `src/ui/ControlPage.cpp` (Lines 417-420)

```cpp
// 读取位置（返回脉冲值）
ZAux_Direct_GetMpos(g_handle, motorID, &fMPos);  // 实际位置（Measured Position）
ZAux_Direct_GetDpos(g_handle, motorID, &fDPos);  // 指令位置（Desired Position）

// 读取速度（返回脉冲/秒）
ZAux_Direct_GetMspeed(g_handle, motorID, &fMVel);  // 实际速度
ZAux_Direct_GetSpeed(g_handle, motorID, &fDVel);   // 指令速度
```

**返回值类型**: 所有API返回的都是**原始脉冲值**（driver值）

---

### ZMotion API运动命令

#### 命令1: 绝对位置运动 (MoveAbs)

**API**: `ZAux_Direct_Single_MoveAbs(g_handle, motorID, targetPulses)`

**含义**: 移动到**绝对脉冲位置**（相对于上电零点）

**示例场景**:
```cpp
// 场景：电机上电后在位置0（零点），用户想移动到100mm位置

// 1. 用户在表格Pos列输入: 100
// 2. 转换为脉冲值
double targetPulses = 100.0 * 13086.9;  // = 1308690脉冲

// 3. 发送绝对运动命令
ZAux_Direct_Single_MoveAbs(g_handle, motorID, 1308690.0);

// 结果：电机移动到相对零点1308690脉冲的位置（即100mm）
```

**安全注意**:
- ✅ 适用于已知绝对位置的场景（如回到安全位置H）
- ⚠️ 若零点不在预期位置，可能导致电机移动到危险区域
- ⚠️ 必须确认当前零点位置后再使用绝对命令

#### 命令2: 相对位置运动 (Move)

**API**: `ZAux_Direct_Single_Move(g_handle, motorID, deltaPulses)`

**含义**: 从**当前位置**偏移指定脉冲数

**示例场景**:
```cpp
// 场景：电机当前在500mm位置，用户想前进100mm

// 1. 用户在表格Pos列输入: 100
// 2. 转换为脉冲值
double deltaPulses = 100.0 * 13086.9;  // = 1308690脉冲

// 3. 发送相对运动命令
ZAux_Direct_Single_Move(g_handle, motorID, 1308690.0);

// 结果：电机从当前位置（500mm）向前移动100mm，到达600mm位置
```

**安全注意**:
- ✅ 适用于微调、点动等场景
- ⚠️ 无法保证目标位置的绝对安全性（可能越界）
- ⚠️ 多次相对运动可能累积误差

---

### ControlPage的命令选择逻辑

**文件**: `src/ui/ControlPage.cpp` (Lines 836-865)

```cpp
// 用户编辑Pos列（列索引2）时触发
case 2: {  // Pos - 位置（需要触发运动）
    // 1. 先取消当前运动
    {
        QMutexLocker locker(&g_mutex);
        if (!g_handle) {
            ret = 1;
            break;
        }
        ZAux_Direct_Single_Cancel(g_handle, motorID, 0);
    }

    // 2. 根据UI选择使用绝对或相对运动
    {
        QMutexLocker locker(&g_mutex);
        if (!g_handle) {
            ret = 1;
            break;
        }
        if (ui->cb_motor_pos_abs->isChecked()) {
            // ✅ 绝对运动模式 - checkbox勾选
            ret = ZAux_Direct_Single_MoveAbs(g_handle, motorID, value);
            qDebug() << "[ControlPage] 触发绝对运动到位置:" << value;
        } else {
            // ⚠️ 相对运动模式 - checkbox未勾选
            ret = ZAux_Direct_Single_Move(g_handle, motorID, value);
            qDebug() << "[ControlPage] 触发相对运动，距离:" << value;
        }
    }
    break;
}
```

**UI控件**: `cb_motor_pos_abs` (QCheckBox)
- **勾选**: 绝对位置模式（MoveAbs）
- **不勾选**: 增量脉冲模式（Move）

---

## 完整转换链示例

### 场景1: 绝对运动到100mm（物理单位模式）

```
用户操作：在Pos列输入100，checkbox勾选（绝对模式）
         ↓
ControlPage::onMotorTableCellChanged()
         ↓
displayValue = 100.0
         ↓
driverValueFromDisplay(100.0, motorID, Position)
         ↓
UnitConverter::physicalToDriver(100.0, AxisUnitInfo{pulsesPerUnit=13086.9}, Position)
         ↓
driverValue = 100.0 * 13086.9 = 1308690.0
         ↓
ZAux_Direct_Single_MoveAbs(g_handle, motorID, 1308690.0)
         ↓
电机移动到绝对位置1308690脉冲（即相对零点100mm）
```

### 场景2: 相对运动50mm（Driver值模式）

```
用户操作：在Pos列输入654345，checkbox不勾选（增量模式），m_displayPhysicalUnits=false
         ↓
ControlPage::onMotorTableCellChanged()
         ↓
displayValue = 654345.0
         ↓
driverValueFromDisplay(654345.0, motorID, Position)  // 无转换，直接返回
         ↓
driverValue = 654345.0（脉冲）
         ↓
ZAux_Direct_Single_Move(g_handle, motorID, 654345.0)
         ↓
电机从当前位置前进654345脉冲（约50mm）
```

### 场景3: AutoTaskPage使用FeedController

```
任务文件：target_depth: "@H"
         ↓
AutoDrillManager::loadSteps()
         ↓
resolvePosition("@H", outDepthMm, errorMsg)
         ↓
getKeyPositionFromFeed("H")
         ↓
FeedController::getKeyPosition("H")  // 返回13100000脉冲
         ↓
FeedController::pulsesToMm(13100000.0)  // 转换为mm
         ↓
mm = 13100000.0 / 13086.9 = 1001.0mm
         ↓
step.targetDepthMm = 1001.0
         ↓
（后续执行时）FeedController::setTargetDepth(1001.0)
         ↓
FeedController::mmToPulses(1001.0)  // 转换回脉冲
         ↓
pulses = 1001.0 * 13086.9 = 13100000.0
         ↓
ZAux_Direct_Single_MoveAbs(g_handle, motorID, 13100000.0)
```

---

## 安全操作指南 ⚠️

### 上电初始化流程（必须遵循）

1. **电机上电前检查**:
   - 确认所有机构处于安全位置（远离极限位置）
   - 确认周围无人员、无障碍物

2. **电机上电**:
   - 电机驱动器上电
   - 当前位置被设为零点（0脉冲）

3. **回零流程**（通过各机构控制器）:
   - FeedController: `initialize()` → 移动到A位置（底部）使用stall detection
   - RotationController: `reset()` → 停止回转
   - 其他控制器: 执行各自的回零逻辑

4. **验证零点**:
   - 在ControlPage中查看各电机MPos值
   - 确认零点位置与预期一致

### ControlPage手动运动安全规则

#### 规则1: 确认运动模式

**操作前必须检查**:
- 查看`cb_motor_pos_abs`复选框状态
- 确认是**绝对模式**还是**增量模式**

**案例 - 危险操作**:
```
电机当前在1000mm位置
用户以为是增量模式，想前进10mm
实际checkbox勾选了绝对模式
输入10 → 电机移动到10mm（绝对位置）→ 反向移动990mm！
```

#### 规则2: 确认显示单位

**操作前必须检查**:
- 查看状态栏显示的单位（"Driver值: XXX脉冲" 或 "物理单位: XXX mm"）
- 确认输入值的单位

**案例 - 危险操作**:
```
用户以为是mm单位，想移动到100mm
实际是Driver值模式
输入100 → 电机移动到100脉冲（约0.0076mm）→ 几乎不动
用户误以为电机故障，重复操作多次
```

#### 规则3: 小幅度测试

**首次运动时**:
1. 使用增量模式（checkbox不勾选）
2. 输入小的物理值（如1mm或5deg）
3. 观察电机运动方向和距离
4. 确认无误后再进行大幅度运动

#### 规则4: 急停准备

**所有运动操作前**:
- 手放在急停按钮上
- 确认急停按钮功能正常
- 知道如何在ControlPage中停止单轴（`ZAux_Direct_Single_Cancel`）

### ControlPage与AutoTaskPage的切换

**互锁机制**:
- MotionLockManager管理运动互斥
- AutoTaskPage运行时，ControlPage手动操作可能被拒绝（需用户确认）

**切换流程**:
1. 停止AutoTaskPage自动任务
2. 等待MotionLockManager释放锁
3. 在ControlPage中执行手动操作

---

## 调试技巧

### 验证转换公式

**方法1: 使用ControlPage**
1. 切换到Driver值模式
2. 记录当前MPos脉冲值（如131000）
3. 切换到物理单位模式
4. 查看MPos显示值（应为131000/13086.9 ≈ 10.01mm）

**方法2: 使用ZDevelop命令**
```basic
' 查询Fz电机（MotorMap[2]）的位置
?MPOS(2)
' 输出: 131000（脉冲）

' 手动计算
?MPOS(2)/13086.9
' 输出: 10.01（mm）
```

### 日志分析

**ControlPage日志关键字**:
```
[ControlPage] 触发绝对运动到位置: 1308690.0
[ControlPage] 触发相对运动，距离: 654345.0
```

**FeedController日志关键字**:
```
[FeedController] mmToPulses(100.0) → 1308690.0
[FeedController] pulsesToMm(1308690.0) → 100.0
```

---

## 常见问题FAQ

### Q1: "脉冲"和"driver值"是同一个东西吗？
**A**: 是的。在本系统中，"脉冲"、"driver值"、"ZMotion API返回值"都是指同一个东西：**原始脉冲计数**。

### Q2: UnitConverter和FeedController有什么区别？
**A**:
- **UnitConverter**: 通用转换器，支持所有轴，基于`config/unit_conversions.csv`
- **FeedController**: 专用于Fz轴，提供局部绝对坐标系（A/H/K等位置），基于`config/mechanisms.json`

### Q3: 为什么ControlPage需要两种显示模式？
**A**:
- **物理单位模式**: 用户友好，直观（如100mm）
- **Driver值模式**: 调试用，显示原始脉冲值（如1308690），便于与ZDevelop对比

### Q4: 绝对运动和相对运动应该用哪个？
**A**:
- **绝对运动** (`MoveAbs`): 已知目标绝对位置时使用（如回到安全位置H）
- **相对运动** (`Move`): 微调、点动时使用（如前进10mm）
- **安全建议**: 首次操作优先使用相对运动 + 小幅度

### Q5: 如何确认电机零点位置是否正确？
**A**:
1. 执行回零流程（通过各机构控制器的`initialize()`）
2. 在ControlPage中查看MPos值
3. 移动到已知物理位置（如A位置底部）
4. 检查MPos值是否符合预期（A位置应为0脉冲）

### Q6: AutoTaskPage使用的坐标系和ControlPage一样吗？
**A**: 不完全一样。
- **AutoTaskPage**: 使用FeedController的局部绝对坐标系（A=0mm, H=1001mm）
- **ControlPage**: 使用电机上电零点 + UnitConverter转换

---

## 版本历史

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|------------|
| v1.0 | 2025-12-24 | DrillControl团队 | 初始版本，详细记录坐标系统与运动命令 |

---

## 参考文档

- `docs/FEED_COORDINATE_SYSTEM_MIGRATION.md` - FeedController坐标系迁移文档
- `docs/MECHANISM_CONTROLLERS_GUIDE.md` - 机构控制器详细指南
- `docs/MOTION_INTERLOCK_SYSTEM.md` - 运动互锁系统设计
- `config/unit_conversions.csv` - 单位转换配置文件
- `config/mechanisms.json` - 机构参数配置文件
