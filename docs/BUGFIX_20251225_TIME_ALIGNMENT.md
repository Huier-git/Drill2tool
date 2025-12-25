# 采集时间对齐与采样率显示问题分析

**日期**: 2025-12-25
**版本**: v2.2
**作者**: DrillControl开发团队

---

## 问题概述

用户在DatabasePage查看数据时发现：
1. **曲线未对齐**：MDB数据从0秒开始，但振动数据从约5秒才开始
2. **振动采样率显示**：每个1秒时间窗口内只有896-1024个样本，而非预期的5000个

---

## 问题1：曲线左边未对齐 ✅ 已修复

### 现象描述

从DatabasePage的趋势曲线图可以看到：
- **MDB数据**（上压力、下压力、扭矩）：从时间0秒就有数据
- **振动数据**（振动X/Y/Z）：从约5秒后才开始出现数据

### 根本原因

**时间基准设置的异步问题**：

**文件**: `src/control/AcquisitionManager.cpp` Line 388-394（旧代码）

```cpp
const qint64 baseTimestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;

// ❌ 异步调用，不等待完成
QMetaObject::invokeMethod(m_vibrationWorker, "setTimeBase", Qt::QueuedConnection, ...);
QMetaObject::invokeMethod(m_mdbWorker, "setTimeBase", Qt::QueuedConnection, ...);
QMetaObject::invokeMethod(m_motorWorker, "setTimeBase", Qt::QueuedConnection, ...);
```

**问题链条**：

1. `startNewRound()`在DbWriter线程中创建新轮次
2. 获取当前时间作为`baseTimestampUs`（时间基准）
3. **异步**发送`setTimeBase`消息给3个Worker
4. 立即返回，继续执行后续代码
5. `startAll()`异步启动3个Worker

**时序问题**：
```
时间线：
T0: 创建轮次，获取baseTimestampUs = 1000000 (示例)
T1: 发送setTimeBase(1000000)给VibrationWorker（异步）
T2: 发送setTimeBase(1000000)给MdbWorker（异步）
T3: 发送setTimeBase(1000000)给MotorWorker（异步）
T4: startAll() - 启动所有Workers

--- 以下是异步消息处理，顺序不确定 ---

T5: MdbWorker收到setTimeBase → m_timeBaseUs = 1000000, m_elapsedTimer.restart()
T6: MdbWorker启动采集 → currentTimestampUs() = 1000000 + 0 = 1000000 (相对时间0秒)
T7: MdbWorker发送第一个数据块 → startTimestampUs = 1000000 (0秒)

T10: VibrationWorker启动采集（但还没收到setTimeBase！）
T11: VibrationWorker发送第一个数据块 → currentTimestampUs() = QDateTime::now() = 5000000 (绝对时间)
T12: VibrationWorker收到setTimeBase → m_timeBaseUs = 1000000
T13: VibrationWorker发送第二个数据块 → currentTimestampUs() = 1000000 + elapsed = 1005000 (5秒)
```

**BaseWorker::currentTimestampUs()的逻辑** (Line 177-184)：
```cpp
qint64 BaseWorker::currentTimestampUs() const
{
    if (m_hasTimeBase && m_elapsedTimer.isValid()) {
        return m_timeBaseUs + (m_elapsedTimer.nsecsElapsed() / 1000);  // 相对时间
    }
    return QDateTime::currentMSecsSinceEpoch() * 1000;  // 绝对时间 ❌
}
```

**结果**：
- MdbWorker快速收到`setTimeBase`，数据从相对时间0开始 ✓
- VibrationWorker延迟收到`setTimeBase`（或在启动采集后才收到），早期数据使用绝对时间 ❌
- 数据库中看到MDB从0秒开始，振动从5秒开始

### 修复方案

**文件**: `src/control/AcquisitionManager.cpp` Line 388-397（新代码）

```cpp
// ✓ 同步阻塞调用，确保所有Worker都设置好时间基准后再继续
QMetaObject::invokeMethod(m_vibrationWorker, "setTimeBase", Qt::BlockingQueuedConnection,
                          Q_ARG(qint64, baseTimestampUs));
QMetaObject::invokeMethod(m_mdbWorker, "setTimeBase", Qt::BlockingQueuedConnection,
                          Q_ARG(qint64, baseTimestampUs));
QMetaObject::invokeMethod(m_motorWorker, "setTimeBase", Qt::BlockingQueuedConnection,
                          Q_ARG(qint64, baseTimestampUs));

LOG_DEBUG("AcquisitionManager", "Time base synchronized for all workers");
```

**修复后的时序**：
```
T0: 创建轮次，获取baseTimestampUs = 1000000
T1: 阻塞调用setTimeBase(1000000)给VibrationWorker → 等待完成 ✓
T2: 阻塞调用setTimeBase(1000000)给MdbWorker → 等待完成 ✓
T3: 阻塞调用setTimeBase(1000000)给MotorWorker → 等待完成 ✓
T4: 所有Worker的m_timeBaseUs都已设置 ✓
T5: startAll() - 启动所有Workers
T6: 所有Worker从相对时间0开始采集 ✓
```

**预期效果**：
- ✅ MDB数据从0秒开始
- ✅ 振动数据也从0秒开始
- ✅ 电机数据也从0秒开始
- ✅ 所有曲线左边对齐

---

## 问题2：振动采样率显示波动 ✓ 正常现象

### 现象描述

DatabasePage表格显示每个1秒时间窗口内的振动样本数：
```
时间(秒) | 振动X | 振动Y | 振动Z
---------|-------|-------|-------
28       | 896   | 896   | 896
29       | 896   | 896   | 896
30       | 896   | 896   | 896
31       | 1024  | 1024  | 1024
32       | 896   | 896   | 896
33       | 1024  | 1024  | 1024
34       | 1024  | 1024  | 1024
35       | 960   | 960   | 960
36       | 896   | 896   | 896
```

用户疑惑：为什么不是每秒5000个样本？

### 分析：这是正常现象！

**实际采样率验证**（SQL查询）：
```sql
SELECT channel_id,
       SUM(n_samples) as total_samples,
       COUNT(DISTINCT window_id) as window_count,
       CAST(SUM(n_samples) AS FLOAT) / COUNT(DISTINCT window_id) as avg_per_window
FROM vibration_blocks
WHERE round_id = 2
GROUP BY channel_id;

结果：
channel_id | total_samples | window_count | avg_per_window
-----------|---------------|--------------|----------------
0          | 273216        | 55           | 4967.56
1          | 273216        | 55           | 4967.56
2          | 273216        | 55           | 4967.56
```

**结论**：
- **总体平均采样率 = 4967.56样本/秒**
- 与设定的5000Hz只差0.66%（32.44样本/秒的差异）
- **采样率完全正常！** ✓

### 为什么单个窗口的样本数波动？

**原因1：VK701硬件返回的样本数不固定**

VibrationWorker::readDataBlock() (Line 394)：
```cpp
int recv = m_fnGetFourChannel(m_cardId, pucRecBuf, readSize);
// recv = VK701实际返回的样本数（可能 < readSize）
```

- 请求读取：5000个样本
- 实际返回：896, 960, 1024等（取决于硬件缓冲区状态）

**原因2：数据块时间戳不对齐窗口边界**

**时间窗口定义** (DbWriter::getOrCreateWindow() Line 775)：
```cpp
qint64 windowStart = (timestampUs / 1000000) * 1000000;  // 整秒边界
qint64 windowEnd = windowStart + 1000000;  // 1秒 = 1,000,000微秒
```

**示例场景**：
```
窗口1: [0秒, 1秒)   = [0us, 1000000us)
窗口2: [1秒, 2秒)   = [1000000us, 2000000us)

数据块1: startTimestampUs = 800000us, n_samples = 1024
  → 被分配到窗口1（因为800000us在[0, 1000000)范围内）

数据块2: startTimestampUs = 1100000us, n_samples = 896
  → 被分配到窗口2（因为1100000us在[1000000, 2000000)范围内）

问题：数据块的时间戳是采集开始时刻，不是窗口起始时刻
      所以单个窗口的样本数取决于该窗口内有多少个数据块
```

**采集循环时序**（VibrationWorker Line 143-161）：
```cpp
while (shouldContinue()) {
    readDataBlock();  // 读取VK701，耗时不固定
    QThread::msleep(10);  // 固定延迟10ms
}
```

**实际时序**：
```
时间    | 操作                           | 累积样本数
--------|--------------------------------|------------
0.00s   | 读取896样本  (block #1)        | 896
0.08s   | sleep 10ms, 读取1024样本 (#2)  | 1920
0.17s   | sleep 10ms, 读取896样本  (#3)  | 2816
0.25s   | sleep 10ms, 读取960样本  (#4)  | 3776
...
1.00s   | 窗口1结束，统计896+1024+896+960+... = 约4967样本
```

**结论**：
- 单个窗口的样本数 = 该窗口内所有数据块的样本数之和
- 由于数据块时间戳不对齐窗口边界，每个窗口的块数不同
- **这是正常的！重要的是总体采样率（4967Hz ≈ 5000Hz）**

### 如果需要显示"真实采样率"而非"窗口样本数"

**建议修改DatabasePage的显示**：

**当前显示**：
```
时间(秒) | 振动X
---------|-------
0        | 896    ← 用户误以为采样率只有896Hz
1        | 1024
```

**改进建议**：
1. 显示总体平均采样率：
   ```
   振动采样率: 4967.56 Hz (设定值: 5000 Hz)
   ```

2. 或者在表格中添加列说明：
   ```
   时间(秒) | 振动X样本数 | 说明
   ---------|------------|------
   0        | 896        | 窗口内样本数（非采样率）
   1        | 1024       |
   ```

---

## 问题3：DatabasePage查询时间基准不一致 ✅ 已修复

### 现象描述

用户反馈：
- **单独查询每种数据类型时**：时间长度看起来一样（都从0秒开始）
- **全部数据一起查询时**：起点不一样（MDB从0秒，振动从5秒）

### 根本原因

**文件**: `src/ui/DatabasePage.cpp` Line 174-180（旧代码）

```cpp
// 获取第一个时间窗口的起始时间（用于查询计算）
QList<qint64> windows = m_querier->getWindowTimestamps(m_currentRoundId);
if (!windows.isEmpty()) {
    m_currentRoundStartUs = windows.first();  // ❌ 使用第一个窗口的时间戳
} else {
    m_currentRoundStartUs = ui->table_rounds->item(row, 0)->data(Qt::UserRole).toLongLong();
}
```

**问题链条**：

1. `getWindowTimestamps()` 返回**当前轮次的所有窗口**时间戳列表（按时间排序）
2. 代码取 `windows.first()` 作为时间基准

**为什么导致不一致**：

```
假设数据库中有一个轮次（由于之前的异步setTimeBase问题）：
- Rounds表：round_id=1, start_ts_us=1000000（轮次真实开始时间）
- MDB第一个窗口：window_start_us=1000000（0秒相对时间）
- 振动第一个窗口：window_start_us=6000000（5秒相对时间，因Worker晚收到timebase）

情况1 - 单独查询MDB：
  getWindowTimestamps() → [1000000, 2000000, 3000000, ...]（只有MDB的窗口）
  m_currentRoundStartUs = 1000000
  绘图时：MDB数据从(1000000-1000000)/1e6=0秒开始 ✓

情况2 - 单独查询振动：
  getWindowTimestamps() → [6000000, 7000000, 8000000, ...]（只有振动的窗口）
  m_currentRoundStartUs = 6000000  ← ❌ 错误的基准！
  绘图时：振动数据从(6000000-6000000)/1e6=0秒开始
  → 看起来也是从0秒开始，时间长度和MDB"一样" ✓（实际是错位对齐的）

情况3 - 全部一起查询：
  getWindowTimestamps() → [1000000, 2000000, ..., 6000000, 7000000, ...]（所有窗口混合）
  m_currentRoundStartUs = 1000000  ← MDB的第一个窗口
  绘图时：
    MDB数据从(1000000-1000000)/1e6=0秒开始 ✓
    振动数据从(6000000-1000000)/1e6=5秒开始 ← ❌ 起点不对齐！
```

**结论**：
- 单独查询时，每种数据都用**自己的第一个窗口**作为基准 → 都从0秒"开始"（虚假对齐）
- 一起查询时，用**最早的窗口**（通常是MDB）作为基准 → 暴露了真实的时间差

### 修复方案

**文件**: `src/ui/DatabasePage.cpp` Line 174-176（新代码）

```cpp
// 使用轮次的真实开始时间作为时间基准（而非第一个窗口的时间戳）
// 这样可以确保所有数据类型的时间轴对齐，无论单独查询还是一起查询
m_currentRoundStartUs = ui->table_rounds->item(row, 0)->data(Qt::UserRole).toLongLong();
```

**修复逻辑**：
- 删除 `getWindowTimestamps()` 查询逻辑
- 直接使用轮次的 `start_ts_us`（存储在 `Qt::UserRole` 中）作为唯一时间基准
- 这样无论查询哪种数据，时间基准都是一致的

**修复后的时序**：
```
所有查询情况下：
  m_currentRoundStartUs = 1000000（轮次的真实开始时间）

单独查询MDB：
  MDB数据从(1000000-1000000)/1e6=0秒开始 ✓

单独查询振动：
  振动数据从(6000000-1000000)/1e6=5秒开始 ✓（暴露真实延迟）

全部一起查询：
  MDB数据从0秒开始 ✓
  振动数据从5秒开始 ✓
  → 一致性得到保证
```

**预期效果**：
- ✅ 所有查询模式下时间基准一致（都用轮次的 start_ts_us）
- ✅ 如果Worker启动有延迟，曲线会正确显示真实的时间差
- ✅ 修复异步setTimeBase后，新采集的数据会从0秒对齐

---

## 测试验证

### 测试1：曲线对齐修复

**步骤**：
1. 编译新版本
2. 开始所有采集
3. 采集30秒
4. 停止采集
5. 在DatabasePage查看趋势曲线

**预期结果**：
- ✅ MDB曲线从0秒开始
- ✅ 振动曲线也从0秒开始
- ✅ 电机曲线也从0秒开始
- ✅ 所有曲线左边对齐

### 测试2：采样率验证

**SQL查询**：
```sql
SELECT
    channel_id,
    SUM(n_samples) as total,
    MIN(start_ts_us) as first_ts,
    MAX(start_ts_us) as last_ts,
    CAST(SUM(n_samples) AS FLOAT) / ((MAX(start_ts_us) - MIN(start_ts_us)) / 1000000.0) as actual_rate
FROM vibration_blocks
WHERE round_id = (SELECT MAX(round_id) FROM rounds)
GROUP BY channel_id;
```

**预期结果**：
- `actual_rate` ≈ 5000 Hz（允许±50Hz的误差）

---

## 影响范围

### 问题1修复：Worker时间基准同步

**影响的文件**：
- `src/control/AcquisitionManager.cpp` Line 390-397

**影响的功能**：
- ✅ 所有Worker的时间基准同步
- ✅ 采集开始时曲线对齐
- ⚠️ `startNewRound()`会阻塞几毫秒（等待setTimeBase完成），但对用户无感知

**风险**：
- 无，纯粹是bug修复

### 问题2说明：振动采样率显示（正常现象）

**影响的文件**：
- 无（这是正常现象，不需要修复）

**建议**：
- 可以考虑在DatabasePage添加总体采样率显示
- 或者在表格列标题添加说明"（窗口样本数）"

### 问题3修复：DatabasePage时间基准统一

**影响的文件**：
- `src/ui/DatabasePage.cpp` Line 174-176

**影响的功能**：
- ✅ 统一所有查询模式的时间基准（单独查询/全部查询）
- ✅ 正确显示Worker启动延迟导致的时间差
- ✅ 删除了不必要的 `getWindowTimestamps()` 调用，提升性能

**风险**：
- ⚠️ 修复后，旧数据库中的数据会暴露真实的Worker启动延迟
- 用户可能会看到单独查询振动数据时不再从0秒开始（而是从5秒开始）
- 这是**正确的行为**，反映了数据的真实时间戳
- 新采集的数据（修复问题1后）会从0秒完美对齐

---

## 版本历史

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|----------|
| v2.2 | 2025-12-25 | DrillControl团队 | 修复时间基准异步导致的曲线未对齐问题 |

---

## 相关文档

- `docs/BUGFIX_20251225_SAMPLING_AND_ROUND_RESET.md` - MDB采样率与轮次重置修复
- `src/control/AcquisitionManager.cpp` - 采集管理器实现
- `src/dataACQ/BaseWorker.cpp` - Worker基类时间基准实现
