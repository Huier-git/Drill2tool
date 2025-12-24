# FeedController坐标系统迁移文档

**作者**: DrillControl开发团队
**日期**: 2025-12-24
**版本**: v2.0

---

## 变更概述

将FeedController的坐标转换系统从**反向坐标系**改为**简单线性坐标系**，统一全系统的坐标表达，消除混淆。

### 关键改动

| 项目 | 旧系统（反向） | 新系统（正向线性） |
|------|---------------|-------------------|
| **零点位置** | 顶部H (1001mm) | 底部A (0mm) |
| **方向** | 向下递减 | 向上递增 |
| **mm→脉冲** | `(maxDepth - mm) × pulsesPerMm` | `mm × pulsesPerMm` |
| **脉冲→mm** | `maxDepth - (pulses / pulsesPerMm)` | `pulses / pulsesPerMm` |
| **H位置** | 0mm（旧系统） | 1001mm（新系统） |
| **A位置** | 1059mm（旧系统） | 0mm（新系统） |

---

## 修改原因

### 问题1: 坐标系混乱

**旧系统存在三套坐标系**：

1. **FeedController**: 反向坐标系（顶部=0, 底部=maxDepth）
2. **UnitConverter**: 正向线性坐标系（底部=0, 顶部=max）
3. **mechanisms.json**: 脉冲值（底部=0脉冲, 顶部=13100000脉冲）

这导致了严重的理解障碍和潜在错误。

### 问题2: 用户困惑

加载`task_standard_drilling.json`时，步骤显示的位置值错误：
- H位置显示为58mm（期望1001mm）
- A位置显示为1059mm（期望0mm）

**根本原因**:
- AutoDrillManager通过`getKeyPositionFromFeed()`解析`@H`
- FeedController使用反向公式: `mm = 1059 - (13100000/13086.9) = 58mm` ❌

### 问题3: 维护困难

反向坐标系需要开发者和用户在脑中反向映射，增加心智负担：
- "H=0mm"实际指顶部1001mm位置
- "移动到100mm"实际是向上移动，接近顶部

---

## 新坐标系定义

### 局部绝对坐标系

- **原点**: A位置（进给机构底部）= 0mm = 0脉冲
- **正方向**: 向上（朝向H位置）
- **最大值**: H位置（进给机构顶部）= 1001mm = 13100000脉冲

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

### 转换公式

```cpp
// mm → 脉冲（简单线性）
double pulses = (mm - zeroOffsetMm) * pulsesPerMm;

// 脉冲 → mm（简单线性）
double mm = (pulses / pulsesPerMm) + zeroOffsetMm;
```

**参数**:
- `pulsesPerMm` = 13086.9（从mechanisms.json读取）
- `zeroOffsetMm` = 0.0（默认值，可通过`setZeroOffset()`修改）

---

## 代码变更

### 文件: `src/control/FeedController.cpp`

#### Line 316-331（转换函数）

**旧代码（反向）**:
```cpp
double FeedController::mmToPulses(double mm) const
{
    // 旧系统：顶部=0mm，向下递增
    double adjustedMm = mm - m_zeroOffsetMm;
    double pulses = (m_config.depthLimits.maxDepthMm - adjustedMm) * m_config.pulsesPerMm;
    return pulses;
}

double FeedController::pulsesToMm(double pulses) const
{
    // 旧系统：顶部=0mm，向下递增
    double mm = m_config.depthLimits.maxDepthMm - (pulses / m_config.pulsesPerMm);
    return mm + m_zeroOffsetMm;
}
```

**新代码（正向线性）**:
```cpp
double FeedController::mmToPulses(double mm) const
{
    // 新系统：简单线性转换，局部绝对坐标系
    // 0mm (A位置底部) = 0脉冲
    // 1001mm (H位置顶部) = 13100000脉冲
    double adjustedMm = mm - m_zeroOffsetMm;
    double pulses = adjustedMm * m_config.pulsesPerMm;
    return pulses;
}

double FeedController::pulsesToMm(double pulses) const
{
    // 新系统：简单线性转换，局部绝对坐标系
    double mm = pulses / m_config.pulsesPerMm;
    return mm + m_zeroOffsetMm;
}
```

---

## 影响范围分析

### ✅ 已验证无影响的模块

#### 1. DrillControlPage
- 使用`setTargetDepth(mm)` → 内部调用`mmToPulses()`
- 使用`currentDepth()` → 内部调用`pulsesToMm()`
- **接口层面不变**，mm↔mm，无需修改

#### 2. ControlPage
- 使用独立的UnitConverter系统（基于unit_conversions.csv）
- UnitConverter也使用正向线性公式：`physical = pulses / pulses_per_unit`
- **两套系统现已统一**，Fz的`pulses_per_unit=13086.9`与FeedController一致

#### 3. AutoDrillManager
- 优先使用任务文件的`positions`字典（直接定义mm值）
- 仅在缺失时调用`getKeyPositionFromFeed()` → `pulsesToMm()`
- **已正确转换**，`@H`现在解析为1001mm

### ⚠️ 需要注意的模块

#### 1. mechanisms.json配置
- `key_positions`仍存储脉冲值：`"H": 13100000`
- `pulsesPerMm`必须准确：`13086.9`
- **零点偏移**: 默认`zeroOffsetMm=0`，若非零需重新校准

#### 2. 旧任务文件
- 如果任务文件未定义`positions`字典，会从mechanisms.json读取
- 确保mechanisms.json的H/K位置正确：
  - H = 13100000脉冲 → 1001mm ✓
  - K ≈ 12000000脉冲 → 917mm ✓

---

## 测试验证

### 单元测试用例

#### Case 1: 脉冲→mm转换
```cpp
// H位置: 13100000脉冲 → 1001mm
double mm = pulsesToMm(13100000.0);
EXPECT_EQ(mm, 1001.0);

// A位置: 0脉冲 → 0mm
mm = pulsesToMm(0.0);
EXPECT_EQ(mm, 0.0);
```

#### Case 2: mm→脉冲转换
```cpp
// H位置: 1001mm → 13100000脉冲
double pulses = mmToPulses(1001.0);
EXPECT_EQ(pulses, 13100000.0);

// K位置: 917mm → ?脉冲
pulses = mmToPulses(917.0);
EXPECT_NEAR(pulses, 917.0 * 13086.9, 0.1);
```

### 集成测试步骤

1. **手动验证**:
   - 使用DrillControlPage将Fz移动到H位置
   - 确认ControlPage显示位置≈1001mm
   - 移动到A位置，确认显示≈0mm

2. **自动任务验证**:
   - 加载`task_standard_drilling.json`
   - 检查步骤表格中的"目标"列：
     - 步骤1（@H）: 1001.0 mm ✓
     - 步骤3（@K）: 917.0 mm ✓
     - 步骤7（@A）: 0.0 mm ✓

3. **短程测试**:
   - 使用`task_test_short_drilling.json`（1/6版本）
   - 验证移动距离正确：H=167mm, K=153mm
   - 确认无越界或反向移动

---

## 配置文件示例

### mechanisms.json (关键部分)
```json
{
  "mechanisms": {
    "Fz": {
      "name": "进给机构",
      "motor_id": 2,
      "pulses_per_mm": 13086.9,
      "key_positions": {
        "A": 0,           // 底部零点（0mm）
        "B": 2620000,     // 200mm
        "C": 5240000,     // 400mm
        "D": 7860000,     // 600mm
        "E": 10480000,    // 800mm
        "H": 13100000,    // 顶部（1001mm）
        "K": 12000000     // 土壤表面（约917mm）
      },
      "depth_limits": {
        "min_depth_mm": 0.0,
        "max_depth_mm": 1059.0
      }
    }
  }
}
```

**注意**: `depth_limits.max_depth_mm`保留为1059mm（机械极限），但实际坐标系只到1001mm（H位置）。

### task_standard_drilling.json (位置定义)
```json
{
  "positions": {
    "H": 1001.0,        // 顶部（直接mm值，优先级高于mechanisms.json）
    "K": 917.0,         // 土壤表面
    "A": 0.0,           // 底部零点
    "STAGE1_END": 687.0,
    "STAGE2_END": 458.0,
    "STAGE3_END": 229.0
  }
}
```

---

## 迁移检查清单

### 开发阶段
- [x] 修改FeedController转换公式
- [x] 更新task_standard_drilling.json位置定义
- [x] 验证ControlPage的UnitConverter一致性
- [x] 创建测试任务文件（task_test_short_drilling.json）
- [x] 更新README.md

### 测试阶段
- [ ] 手动移动Fz到H/K/A位置，确认mm值正确
- [ ] 加载标准任务，确认步骤目标位置显示正确
- [ ] 执行短程测试任务，验证移动距离准确
- [ ] 检查零点偏移为0（`m_zeroOffsetMm == 0.0`）
- [ ] 验证DrillControlPage和AutoTaskPage位置一致

### 文档阶段
- [x] 编写坐标系统迁移文档（本文档）
- [ ] 更新AUTO_TASK_CONFIG_GUIDE.md（位置引用说明）
- [ ] 更新MECHANISM_CONTROLLERS_GUIDE.md（FeedController章节）

---

## 常见问题FAQ

### Q1: 为什么不直接用Modbus位置传感器的值？
**A**: Modbus传感器测量的是绝对物理位置（相对于某个外部基准），而进给机构使用局部相对坐标系（相对于机构本身的底部）。两者不同，不可混用。

### Q2: 零点偏移什么时候非零？
**A**: 当用户手动调零时（如将当前位置设为新零点），`setZeroOffset(offset)`会被调用。默认情况下为0。

### Q3: 为什么depthLimits.maxDepthMm是1059mm而不是1001mm？
**A**:
- **1001mm**: H位置（安全顶部，进给轴的常规工作范围）
- **1059mm**: 机械极限（理论最大行程，包含安全余量）

实际使用中应以H(1001mm)为顶部。

### Q4: 旧的任务文件会失效吗？
**A**: 不会。如果任务文件已定义`positions`字典（直接使用mm值），则不受影响。如果使用`@位置名`引用mechanisms.json，需确保mechanisms.json的脉冲值正确。

### Q5: 如何验证转换公式是否正确？
**A**:
```cpp
// 验证公式
double testPulses = 13100000.0;  // H位置
double testMm = testPulses / 13086.9;  // = 1001.0mm
assert(abs(testMm - 1001.0) < 0.01);
```

---

## 版本历史

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|---------|
| v1.0 | 2025-12-24 | DrillControl团队 | 初始版本，记录反向→正向线性迁移 |

---

## 参考文档

- `docs/MECHANISM_CONTROLLERS_GUIDE.md` - 机构控制器详细指南
- `docs/AUTO_TASK_CONFIG_GUIDE.md` - 自动任务配置指南
- `config/mechanisms.json` - 机构参数配置文件
- `config/unit_conversions.csv` - 单位转换配置
