# 运动互锁系统设计文档

## 概述

本文档描述了 DrillControl 系统中的运动互锁机制，确保运动控制的线程安全性和操作安全性。

## 设计背景

### 问题分析

在原有架构中存在以下问题：

1. **双句柄问题**：`MotorWorker` 和 `ZMotionDriver` 各自维护独立的句柄
2. **竞争风险**：多个线程可能同时访问运动控制器
3. **缺乏互锁**：手动操作和自动脚本可能同时执行，存在安全隐患

### 解决方案

采用**单一句柄 + 全局互斥锁 + 运动互锁管理器**的架构：

```
┌─────────────────────────────────────────────────────────────────┐
│                        主线程 (UI)                               │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │ ControlPage │  │DrillControl │  │   MotionLockManager     │ │
│  │  (手动控制)  │  │    Page     │  │    (运动互锁管理)        │ │
│  └──────┬──────┘  └──────┬──────┘  └────────────┬────────────┘ │
│         │                │                       │               │
│         └────────────────┼───────────────────────┘               │
│                          │                                       │
│                          ▼                                       │
│              ┌───────────────────────┐                          │
│              │   g_handle + g_mutex   │                          │
│              │   (全局单一句柄+互斥锁) │                          │
│              └───────────┬───────────┘                          │
└──────────────────────────┼──────────────────────────────────────┘
                           │
        ┌──────────────────┼──────────────────┐
        │                  │                  │
        ▼                  ▼                  ▼
┌───────────────┐  ┌───────────────┐  ┌───────────────┐
│ VibrationThread│  │  MotorThread  │  │   DbThread    │
│ (5000Hz采集)   │  │ (100Hz只读)   │  │  (数据写入)   │
└───────────────┘  └───────────────┘  └───────────────┘
```

## 核心组件

### 1. 全局句柄与互斥锁 (`Global.h/cpp`)

```cpp
// include/Global.h
extern ZMC_HANDLE g_handle;  // 全局唯一句柄
extern QMutex g_mutex;       // 保护句柄的互斥锁
extern int MotorMap[10];     // 电机映射表
```

**使用规则**：
- 所有 `ZAux_*` API 调用必须在持有 `g_mutex` 的情况下进行
- 使用 `QMutexLocker` 自动管理锁的获取和释放

### 2. 运动互锁管理器 (`MotionLockManager`)

**文件位置**：
- `include/control/MotionLockManager.h`
- `src/control/MotionLockManager.cpp`

**运动来源类型**：
```cpp
enum class MotionSource {
    None,           // 空闲
    ManualJog,      // 手动点动
    ManualAbs,      // 手动绝对运动
    AutoScript,     // 自动脚本（机构控制器）
    Homing          // 回零
};
```

**核心功能**：

| 方法 | 说明 |
|------|------|
| `requestMotion()` | 请求运动许可，冲突时弹窗确认 |
| `releaseMotion()` | 释放运动许可 |
| `emergencyStop()` | 急停，无条件停止所有运动 |
| `isIdle()` | 检查是否空闲 |

**冲突处理流程**：

```
新运动请求 → 检查当前状态
     │
     ├─ 空闲 → 直接获得许可
     │
     └─ 有冲突 → 弹出警告对话框
              │
              ├─ 用户确认 → 停止当前运动 → 获得许可
              │
              └─ 用户取消 → 拒绝请求
```

### 3. 机构控制器基类 (`BaseMechanismController`)

**运动锁相关方法**：

```cpp
// 请求运动互锁
bool requestMotionLock(const QString& description);

// 释放运动互锁
void releaseMotionLock();

// 检查是否持有锁
bool hasMotionLock() const;
```

**使用示例**：

```cpp
bool FeedController::moveToPosition(double position)
{
    // 1. 请求运动锁
    if (!requestMotionLock("进给移动到 " + QString::number(position))) {
        return false;  // 用户取消或被拒绝
    }

    // 2. 执行运动
    // ... 运动代码 ...

    // 3. 释放运动锁
    releaseMotionLock();
    return true;
}
```

### 4. 数据采集线程 (`MotorWorker`)

**特点**：
- 只读访问，不参与运动互锁
- 使用 `g_mutex` 保护数据读取
- 不会阻塞运动控制

```cpp
bool MotorWorker::readMotorPosition(int motorId, double &position)
{
    QMutexLocker locker(&g_mutex);  // 短暂持锁读取
    if (!g_handle) return false;

    int result = ZAux_Direct_GetMpos(g_handle, motorId, &position);
    return result == 0;
}
```

## 线程架构

| 线程 | 用途 | 频率 | 互锁 |
|------|------|------|------|
| 主线程 | UI + 运动控制 | - | 需要 |
| VibrationThread | VK701采集 | 5000Hz | 不需要 |
| MdbThread | Modbus采集 | 10Hz | 不需要 |
| MotorThread | 电机参数采集 | 100Hz | 只读 |
| DbThread | 数据库写入 | 异步 | 不需要 |

## 安全机制

### 1. 互锁规则

- **手动操作** 可以打断 **自动脚本**
- **自动脚本** 可以打断 **手动操作**
- **急停** 无条件立即停止所有运动
- **连续点动** 不触发冲突（同类型覆盖）

### 2. 急停实现

```cpp
void MotionLockManager::emergencyStop()
{
    // 1. 立即停止所有电机
    doStopAll();  // 调用 ZAux_Direct_Rapidstop(g_handle, 2)

    // 2. 清除运动状态
    m_currentSource = MotionSource::None;

    // 3. 发送信号通知
    emit emergencyStopTriggered();
}
```

### 3. 冲突对话框

当检测到运动冲突时，显示确认对话框：

```
┌─────────────────────────────────────┐
│        运动冲突警告                  │
├─────────────────────────────────────┤
│ 检测到运动冲突！                     │
│                                     │
│ 当前运动：手动点动                   │
│   描述：X轴正向点动                  │
│                                     │
│ 请求运动：自动脚本                   │
│   描述：FeedController: 进给到100mm │
│                                     │
│ 是否停止当前运动并执行新操作？       │
│                                     │
│     [停止并执行]    [取消]          │
└─────────────────────────────────────┘
```

## 文件变更清单

### 新增文件

| 文件 | 说明 |
|------|------|
| `include/control/MotionLockManager.h` | 运动互锁管理器头文件 |
| `src/control/MotionLockManager.cpp` | 运动互锁管理器实现 |

### 修改文件

| 文件 | 变更内容 |
|------|----------|
| `include/Global.h` | 添加 `extern QMutex g_mutex;` |
| `src/Global.cpp` | 添加 `QMutex g_mutex;` 定义 |
| `include/dataACQ/MotorWorker.h` | 移除 `m_handle`，使用全局句柄 |
| `src/dataACQ/MotorWorker.cpp` | 所有读取方法使用 `g_mutex` |
| `include/control/ZMotionDriver.h` | 移除 `m_mutex`，使用全局互斥锁 |
| `src/control/ZMotionDriver.cpp` | 所有方法使用 `g_mutex` |
| `include/control/BaseMechanismController.h` | 添加运动锁方法 |
| `src/control/BaseMechanismController.cpp` | 实现运动锁方法 |
| `DrillControl.pro` | 添加 MotionLockManager 文件 |

### 删除文件

| 文件 | 原因 |
|------|------|
| `include/control/MotionController.h` | 功能合并到 MotionLockManager |
| `src/control/MotionController.cpp` | 功能合并到 MotionLockManager |

## 使用指南

### 手动控制页面 (ControlPage)

```cpp
// 点动操作
void ControlPage::onJogPressed(int axis, int direction)
{
    if (!MotionLockManager::instance()->requestMotion(
            MotionSource::ManualJog,
            QString("轴%1 %2点动").arg(axis).arg(direction > 0 ? "正向" : "负向"))) {
        return;
    }

    // 执行点动...
}

void ControlPage::onJogReleased()
{
    // 停止点动...
    MotionLockManager::instance()->releaseMotion(MotionSource::ManualJog);
}
```

### 机构控制器

```cpp
// 在 xxxController 中
bool MyController::doSomeMotion()
{
    // 使用基类方法请求锁
    if (!requestMotionLock("执行某运动")) {
        return false;
    }

    // 运动代码...

    releaseMotionLock();
    return true;
}
```

### 急停按钮

```cpp
void MainWindow::onEmergencyStop()
{
    MotionLockManager::instance()->emergencyStop();
}
```

## 注意事项

1. **主线程调用**：`requestMotion()` 可能弹出对话框，必须在主线程调用
2. **及时释放**：运动完成后必须调用 `releaseMotion()`
3. **异常处理**：使用 RAII 确保锁在异常时也能释放
4. **只读安全**：数据采集线程只读访问，短暂持锁，不影响控制

## 版本历史

| 日期 | 版本 | 变更 |
|------|------|------|
| 2025-01-26 | 1.0 | 初始版本，实现运动互锁机制 |
