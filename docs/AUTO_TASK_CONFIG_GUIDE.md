# 钻机高级控制 - 自动任务参数配置指南

## 1. 概述

钻机高级控制（AutoTask）系统通过 JSON 配置文件实现全流程自动化钻进，无需修改代码即可调整钻进参数、阶段划分和安全阈值。

**核心配置文件：**
- `config/mechanisms.json` - 机构运动参数和关键位置定义（主要配置）
- `config/auto_tasks/*.json` - 自动任务流程定义（引用机构参数）

**设计原则：**
- `mechanisms.json` 定义基础位置（H、K、A等常用位置）
- 任务文件可以定义额外的中间位置（如钻进阶段终点）
- 使用 `@` 前缀引用位置（如 `"target_depth": "@H"`）
- 支持热加载（修改后自动生效，无需重启）

---

## 2. 配置文件结构

### 2.1 mechanisms.json - 机构参数配置

**文件路径：** `config/mechanisms.json`

**作用：** 定义9个钻机机构的运动参数、脉冲转换系数、关键位置等核心参数

#### 完整结构示例（Fz进给机构）

```json
{
  "_version": "2.1",
  "_comment": "钻机机构运动参数配置文件",
  "_last_modified": "2025-01-26",

  "mechanisms": {
    "Fz": {
      "name": "进给机构",
      "motor_id": 2,
      "connection_type": "ethercat",
      "control_mode": "position",

      // 运动参数（脉冲单位）
      "speed": 30857.0,
      "acceleration": 154285.0,
      "deceleration": 154285.0,

      // 行程限制（脉冲单位）
      "max_position": 13100000,    // 顶部极限
      "min_position": 0,           // 底部极限

      // 单位转换
      "pulses_per_mm": 13086.9,    // 脉冲/毫米转换系数

      // 安全位置
      "safe_position": 13100000,   // 安全位（顶部）
      "work_position": 0,          // 工作位（底部）

      // 稳定性检测
      "stable_threshold": 1.0,     // 位置稳定阈值(脉冲)
      "stable_count": 5,           // 稳定计数
      "monitor_interval": 100,     // 监测间隔(ms)
      "position_tolerance": 100.0, // 到位容差(脉冲)

      // 关键位置定义（脉冲单位）
      "key_positions": {
        "A": 0,          // 底部极限
        "B": 1000000,
        "C": 1500000,
        "D": 6000000,
        "E": 7000000,
        "F": 8000000,
        "G": 9000000,
        "H": 13100000,   // 顶部极限（安全位）
        "I": 2000000,
        "J": 11000000,
        "K": 12000000    // 土壤表面位置
      }
    }
  }
}
```

#### 关键参数说明

| 参数 | 说明 | 单位 | 备注 |
|------|------|------|------|
| `motor_id` | EtherCAT电机轴号 | - | 0-7，对应MotorMap映射 |
| `connection_type` | 连接方式 | - | ethercat / modbus |
| `control_mode` | 控制模式 | - | position / velocity / torque |
| `speed` | 默认速度 | 脉冲/s | 位置模式有效 |
| `acceleration` | 加速度 | 脉冲/s² | - |
| `deceleration` | 减速度 | 脉冲/s² | - |
| `max_position` | 正向行程限制 | 脉冲 | 软限位 |
| `min_position` | 负向行程限制 | 脉冲 | 软限位 |
| `pulses_per_mm` | 脉冲/毫米转换系数 | 脉冲/mm | 用于mm↔脉冲转换 |
| `safe_position` | 安全位置 | 脉冲 | 复位/急停后的目标位 |
| `work_position` | 工作位置 | 脉冲 | 初始工作点 |
| `stable_threshold` | 稳定阈值 | 脉冲 | 堵转检测用 |
| `position_tolerance` | 到位容差 | 脉冲 | 定位精度 |
| `key_positions` | 关键位置字典 | 脉冲 | A-Z命名的特殊位置 |

---

### 2.2 任务配置文件 - task_*.json

**文件路径：** `config/auto_tasks/task_*.json`

**作用：** 定义自动钻进流程的步骤序列、参数预设、安全条件

#### 完整结构示例

```json
{
  "task_name": "标准钻进流程",
  "description": "从顶部定位到土壤表面，执行3阶段钻进，完成后返回顶部",
  "version": "1.1",
  "author": "DrillControl开发团队",

  // 新增：任务特定的位置字典（可选）
  "positions": {
    "STAGE1_END": 617.0,     // 第1阶段终点 (mm)
    "STAGE2_END": 317.0,     // 第2阶段终点 (mm)
    "STAGE3_END": 84.0       // 第3阶段终点 (mm)
  },

  // 参数预设库（可被steps引用）
  "presets": {
    "P_IDLE": {
      "id": "P_IDLE",
      "description": "空载移动参数（无回转无冲击）",

      // 进给参数
      "vp_mm_per_min": 100.0,     // 进给速度(mm/min)
      "rpm": 0.0,                  // 回转速度(rpm)
      "fi_hz": 0.0,                // 冲击频率(Hz)

      // 保护参数
      "torque_limit_nm": 500.0,           // 扭矩限制(N·m)
      "pressure_limit_n": 5000.0,         // 压力限制(N)
      "drill_string_weight_n": 380.0,     // 钻杆重量(N)

      // 堵转检测
      "stall_velocity_mm_per_min": 10.0,  // 堵转速度阈值
      "stall_window_ms": 2000,            // 堵转检测窗口

      // SafetyWatchdog参数
      "upper_force_limit": 500.0,              // 上力传感器限制(N)
      "lower_force_limit": 0.0,                // 下力传感器限制(N)
      "emergency_force_limit": 600.0,          // 紧急停机力限(N)
      "max_feed_speed_mm_per_min": 300.0,      // 最大进给速度(mm/min)
      "velocity_change_limit_mm_per_sec": 50.0, // 速度变化率限制
      "position_deviation_limit_mm": 20.0,     // 位置偏差限制(mm)
      "dead_zone_width_n": 50.0,               // 死区宽度(N)
      "dead_zone_hysteresis_n": 5.0            // 死区滞环(N)
    },

    "P_DRILL_STAGE1": {
      "id": "P_DRILL_STAGE1",
      "description": "钻进第1阶段（浅层软地层，低速钻进）",
      "vp_mm_per_min": 30.0,
      "rpm": 30.0,
      "fi_hz": 0.0,
      "torque_limit_nm": 1000.0,
      "pressure_limit_n": 8000.0,
      "drill_string_weight_n": 380.0,
      "stall_velocity_mm_per_min": 5.0,
      "stall_window_ms": 1000,
      "upper_force_limit": 600.0,
      "lower_force_limit": 50.0,
      "emergency_force_limit": 700.0,
      "max_feed_speed_mm_per_min": 150.0,
      "velocity_change_limit_mm_per_sec": 25.0,
      "position_deviation_limit_mm": 10.0,
      "dead_zone_width_n": 100.0,
      "dead_zone_hysteresis_n": 10.0
    }
  },

  // 步骤序列
  "steps": [
    {
      "type": "positioning",           // 步骤类型：定位
      "target_depth": "@H",            // 引用 mechanisms.json 的 H 位置
      "param_id": "P_IDLE",            // 使用的参数预设
      "timeout": 60,                   // 超时时间(秒)
      "description": "步骤1: 空载移动到H位置（顶部）"
    },
    {
      "type": "hold",                  // 步骤类型：暂停
      "requires_user_confirmation": true,  // 需要用户确认
      "description": "步骤2: 等待安装钻管 - 请点击「继续」按钮"
    },
    {
      "type": "positioning",
      "target_depth": "@K",            // 引用 mechanisms.json 的 K 位置
      "param_id": "P_IDLE",
      "timeout": 30,
      "description": "步骤3: 空载移动到K位置（土壤表面）"
    },
    {
      "type": "drilling",              // 步骤类型：钻进
      "target_depth": "@STAGE1_END",   // 引用任务文件的 positions
      "param_id": "P_DRILL_STAGE1",    // 使用的参数预设
      "timeout": 600,                  // 超时时间(秒)
      "description": "步骤4: 第1阶段钻进（浅层0-300mm，30rpm低速）",

      // 安全停止条件（可选）
      "conditions": {
        "stop_if": [
          { "sensor": "force_upper", "op": ">", "value": 600 },
          { "sensor": "force_lower", "op": ">", "value": 600 },
          { "sensor": "torque", "op": ">", "value": 1000 },
          { "sensor": "feed_velocity", "op": ">", "value": 150 }
        ],
        "logic": "OR",              // 逻辑关系：OR / AND
        "abort_on_stop": true       // 是否中止任务
      }
    },
    {
      "type": "drilling",
      "target_depth": 500.0,           // 也可以直接使用 mm 数值
      "param_id": "P_DRILL_STAGE2",
      "timeout": 600,
      "description": "步骤5: 第2阶段钻进（使用硬编码深度）"
    }
  ],

  "notes": [
    "=== 任务说明 ===",
    "此任务文件用于标准钻进流程，仅控制进给(Fz)和回转(Pr)机构",
    "",
    "=== 位置定义 ===",
    "  H位置（1001mm）: 进给机构顶部",
    "  K位置（917mm）: 土壤表面，钻进起始点",
    "  钻进终点（84mm）: 距顶部917mm深度"
  ]
}
```

---

## 3. Fz进给机构关键位置对照表

### 3.1 坐标系说明

**重要：** Fz进给机构使用 **向上为正** 的坐标系：

```
高度(mm)  脉冲值        物理位置
1001  ←  13100000   ← H（顶部，安全位）
 917  ←  12000000   ← K（土壤表面）
 840  ←  11000000   ← J
 688  ←  9000000    ← G
 611  ←  8000000    ← F
 535  ←  7000000    ← E
 459  ←  6000000    ← D
 153  ←  2000000    ← I
 115  ←  1500000    ← C
  76  ←  1000000    ← B
   0  ←  0          ← A（底部极限）

向下钻进 = 脉冲值减小 = 高度减小
```

### 3.2 关键位置对照表

| 位置键 | 脉冲值 | 高度 (mm) | 物理位置描述 | 任务中的使用 |
|--------|---------|-----------|--------------|--------------|
| **H** | 13100000 | **1001.0** | **进给机构顶部（安全位）** | ✅ 初始位置和返回位置 |
| **K** | 12000000 | **916.9** | **土壤表面（钻进起始点）** | ✅ 所有钻进任务的起点 |
| J | 11000000 | 840.5 | 接近顶部区域 | ❌ 预留位置 |
| G | 9000000 | 687.7 | 中上部区域 | ❌ 预留位置 |
| F | 8000000 | 611.4 | 中部区域 | ❌ 预留位置 |
| E | 7000000 | 535.0 | 中部区域 | ❌ 预留位置 |
| D | 6000000 | 458.6 | 中下部区域 | ❌ 预留位置 |
| I | 2000000 | 152.8 | 下部区域 | ❌ 预留位置 |
| C | 1500000 | 114.6 | 接近底部区域 | ❌ 预留位置 |
| B | 1000000 | 76.4 | 接近底部区域 | ❌ 预留位置 |
| A | 0 | **0.0** | **进给机构底部（机械极限）** | ❌ 极限位置 |

**转换公式：**
```
高度(mm) = 脉冲值 / 13086.9
脉冲值 = 高度(mm) × 13086.9
```

### 3.3 钻进任务常用位置

当前标准钻进任务使用的位置（mm单位）：

| 阶段 | 目标深度 (mm) | 对应脉冲值 | 说明 |
|------|---------------|------------|------|
| 初始化 | 1001.0 | 13100000 | H位置：顶部安全位 |
| 定位 | 917.0 | 12000000 | K位置：土壤表面 |
| 第1阶段终点 | 617.0 | 8071174 | ⚠️ 未在key_positions中定义 |
| 第2阶段终点 | 317.0 | 4147652 | ⚠️ 未在key_positions中定义 |
| 第3阶段终点 | 84.0 | 1099300 | ⚠️ 未在key_positions中定义 |

---

## 4. 参数预设系统

### 4.1 预设参数分类

每个预设（Preset）包含以下5类参数：

#### A. 运动参数
| 参数 | 说明 | 单位 | 典型值范围 |
|------|------|------|-----------|
| `vp_mm_per_min` | 进给速度 | mm/min | 30-100 |
| `rpm` | 回转速度 | rpm | 0-90 |
| `fi_hz` | 冲击频率 | Hz | 0-5 |

#### B. 保护参数
| 参数 | 说明 | 单位 | 典型值范围 |
|------|------|------|-----------|
| `torque_limit_nm` | 扭矩限制 | N·m | 500-1400 |
| `pressure_limit_n` | 压力限制 | N | 5000-12000 |
| `drill_string_weight_n` | 钻杆重量补偿 | N | 380（固定值） |

#### C. 堵转检测参数
| 参数 | 说明 | 单位 | 典型值范围 |
|------|------|------|-----------|
| `stall_velocity_mm_per_min` | 堵转速度阈值 | mm/min | 5-10 |
| `stall_window_ms` | 堵转检测窗口 | ms | 1000-2000 |

#### D. SafetyWatchdog参数（力传感器监测）
| 参数 | 说明 | 单位 | 典型值范围 |
|------|------|------|-----------|
| `upper_force_limit` | 上力传感器限制 | N | 500-750 |
| `lower_force_limit` | 下力传感器限制 | N | 0-50 |
| `emergency_force_limit` | 紧急停机力限 | N | 600-850 |

#### E. 速度监测参数
| 参数 | 说明 | 单位 | 典型值范围 |
|------|------|------|-----------|
| `max_feed_speed_mm_per_min` | 最大进给速度 | mm/min | 150-300 |
| `velocity_change_limit_mm_per_sec` | 速度变化率限制 | mm/s | 25-50 |
| `position_deviation_limit_mm` | 位置偏差限制 | mm | 10-20 |

#### F. 死区参数（避免抖动）
| 参数 | 说明 | 单位 | 典型值范围 |
|------|------|------|-----------|
| `dead_zone_width_n` | 死区宽度 | N | 50-100 |
| `dead_zone_hysteresis_n` | 死区滞环 | N | 5-10 |

### 4.2 预设使用示例

**P_IDLE（空载移动）：**
```json
{
  "vp_mm_per_min": 100.0,  // 快速移动
  "rpm": 0.0,              // 不回转
  "fi_hz": 0.0,            // 不冲击
  "upper_force_limit": 500.0  // 较低的力限制
}
```

**P_DRILL_STAGE1（浅层软地层）：**
```json
{
  "vp_mm_per_min": 30.0,   // 慢速钻进
  "rpm": 30.0,             // 低速回转
  "fi_hz": 0.0,            // 不冲击
  "upper_force_limit": 600.0  // 适中的力限制
}
```

**P_DRILL_STAGE3（深层硬地层）：**
```json
{
  "vp_mm_per_min": 70.0,   // 较快钻进
  "rpm": 90.0,             // 高速回转
  "fi_hz": 0.0,            // 不冲击
  "upper_force_limit": 750.0  // 较高的力限制
}
```

---

## 5. 步骤类型详解

### 5.1 positioning（定位步骤）

**用途：** 空载移动到指定位置

**必需参数：**
```json
{
  "type": "positioning",
  "target_depth": "@H",      // 目标位置（可用@引用、或mm数值）
  "param_id": "P_IDLE",      // 使用的参数预设
  "timeout": 60,             // 超时时间(秒)
  "description": "说明文字"
}
```

**执行逻辑：**
1. 应用 `param_id` 对应的预设参数
2. 启动 Fz 机构移动到 `target_depth`
3. 等待到位或超时
4. 成功则进入下一步，超时则中止任务

---

### 5.2 drilling（钻进步骤）

**用途：** 执行钻进作业

**必需参数：**
```json
{
  "type": "drilling",
  "target_depth": "@STAGE1_END", // 目标深度（可用@引用、或mm数值）
  "param_id": "P_DRILL_STAGE1",  // 使用的参数预设
  "timeout": 600,                // 超时时间(秒)
  "description": "说明文字"
}
```

**可选参数：**
```json
{
  "conditions": {
    "stop_if": [
      { "sensor": "force_upper", "op": ">", "value": 600 },
      { "sensor": "torque", "op": ">", "value": 1000 }
    ],
    "logic": "OR",           // OR: 任一条件触发即停止；AND: 全部满足才停止
    "abort_on_stop": true    // true: 中止任务；false: 仅停止当前步骤
  }
}
```

**支持的传感器类型：**
| sensor | 说明 | 单位 |
|--------|------|------|
| `force_upper` | 上力传感器 | N |
| `force_lower` | 下力传感器 | N |
| `torque` | 扭矩 | N·m |
| `feed_velocity` | 进给速度 | mm/min |

**支持的运算符：**
- `>` - 大于
- `<` - 小于
- `>=` - 大于等于
- `<=` - 小于等于
- `==` - 等于

**执行逻辑：**
1. 应用 `param_id` 对应的预设参数（包括Fz/Pr/Pi三个机构）
2. 启动钻进作业
3. 实时监测 `conditions` 中的传感器数据
4. 任一条件触发或到达 `target_depth` 或超时则停止
5. 根据 `abort_on_stop` 决定是否继续任务

---

### 5.3 hold（暂停步骤）

**用途：** 暂停流程，等待用户操作或延时

**参数：**
```json
{
  "type": "hold",
  "hold_time": 5,                    // 延时时间(秒)，0表示无限等待
  "requires_user_confirmation": true, // 是否需要用户点击「继续」
  "description": "说明文字"
}
```

**执行逻辑：**
- 若 `requires_user_confirmation = true`：显示对话框，等待用户点击「继续」或「停止」
- 若 `requires_user_confirmation = false`：延时 `hold_time` 秒后自动继续

---

## 6. 安全监测机制

### 6.1 SafetyWatchdog实时监测

**监测参数：**
| 监测项 | 参数名 | 说明 |
|--------|--------|------|
| 上力传感器 | `upper_force_limit` | 超限立即停机 |
| 下力传感器 | `lower_force_limit` | 超限立即停机 |
| 紧急力限 | `emergency_force_limit` | 最后防线 |
| 进给速度 | `max_feed_speed_mm_per_min` | 速度异常停机 |
| 速度变化率 | `velocity_change_limit_mm_per_sec` | 防止突变 |
| 位置偏差 | `position_deviation_limit_mm` | 防止失控 |

**触发后果：**
- 立即停止所有运动机构（Fz/Pr/Pi）
- 显示故障类型（FORCE_UPPER_LIMIT / MAX_FEED_SPEED 等）
- 任务状态变为 `Error`
- 需要手动复位后才能继续

### 6.2 堵转检测

**检测条件：**
```
IF (实际进给速度 < stall_velocity_mm_per_min) AND (持续时间 > stall_window_ms)
THEN 触发堵转报警
```

**典型参数：**
- 空载移动：`stall_velocity_mm_per_min = 10`, `stall_window_ms = 2000`
- 钻进作业：`stall_velocity_mm_per_min = 5`, `stall_window_ms = 1000`

### 6.3 steps条件监测

**独立于SafetyWatchdog的任务级监测：**
```json
"conditions": {
  "stop_if": [
    { "sensor": "force_upper", "op": ">", "value": 600 }
  ],
  "logic": "OR",
  "abort_on_stop": true
}
```

**区别：**
- SafetyWatchdog：全局监测，任何时刻都有效，触发后必须手动复位
- steps条件：步骤级监测，仅在该步骤执行时有效，可选择是否中止任务

---

## 7. 配置修改指南

### 7.1 修改关键位置（mechanisms.json）

**场景：** 调整土壤表面位置（K位置）

**步骤：**
1. 打开 `config/mechanisms.json`
2. 找到 `Fz` → `key_positions` → `K`
3. 修改脉冲值（例如从 12000000 改为 11950000）
4. 保存文件（系统自动热加载）

**或者修改mm值：**
```python
# 假设新的K位置高度为 910mm
新脉冲值 = 910 * 13086.9 = 11909089
```

**优势：**
- 使用位置引用语法（`"target_depth": "@K"`）的任务文件会自动使用新位置
- 无需修改任务文件即可全局更新位置定义

---

### 7.2 添加新的钻进阶段

**场景：** 在标准3阶段钻进任务中增加第4阶段（100rpm超高速）

**步骤：**

**① 在 positions 中添加新位置：**
```json
"positions": {
  "STAGE1_END": 617.0,
  "STAGE2_END": 317.0,
  "STAGE3_END": 84.0,
  "STAGE4_END": 50.0      // 新增：第4阶段终点
}
```

**② 在 presets 中添加新预设：**
```json
"P_DRILL_STAGE4": {
  "id": "P_DRILL_STAGE4",
  "description": "钻进第4阶段（极深层，超高速）",
  "vp_mm_per_min": 90.0,
  "rpm": 100.0,
  "fi_hz": 0.0,
  "torque_limit_nm": 1600.0,
  "pressure_limit_n": 14000.0,
  "drill_string_weight_n": 380.0,
  "stall_velocity_mm_per_min": 5.0,
  "stall_window_ms": 1000,
  "upper_force_limit": 800.0,
  "lower_force_limit": 50.0,
  "emergency_force_limit": 900.0,
  "max_feed_speed_mm_per_min": 220.0,
  "velocity_change_limit_mm_per_sec": 40.0,
  "position_deviation_limit_mm": 10.0,
  "dead_zone_width_n": 100.0,
  "dead_zone_hysteresis_n": 10.0
}
```

**③ 在 steps 中插入新步骤（在第3阶段之后）：**
```json
{
  "type": "drilling",
  "target_depth": "@STAGE4_END",  // 引用 positions 中的位置
  "param_id": "P_DRILL_STAGE4",
  "timeout": 600,
  "description": "步骤6.5: 第4阶段钻进（极深层，100rpm超高速）",
  "conditions": {
    "stop_if": [
      { "sensor": "force_upper", "op": ">", "value": 800 },
      { "sensor": "torque", "op": ">", "value": 1600 }
    ],
    "logic": "OR",
    "abort_on_stop": true
  }
}
```

**④ 保存文件并重新加载任务**

---

### 7.3 调整安全阈值

**场景：** 降低第1阶段的力限制，防止浅层地层过载

**步骤：**
1. 打开任务配置文件（例如 `task_standard_drilling.json`）
2. 找到 `P_DRILL_STAGE1` 预设
3. 修改 `upper_force_limit` 从 600 降低到 550
4. 修改 steps 中的对应条件：
```json
"conditions": {
  "stop_if": [
    { "sensor": "force_upper", "op": ">", "value": 550 }  // 从600改为550
  ]
}
```
5. 保存文件

---

### 7.4 添加用户确认点

**场景：** 在第1和第2阶段之间插入确认点，允许检查钻头状态

**步骤：**
在 steps 数组中插入：
```json
{
  "type": "hold",
  "requires_user_confirmation": true,
  "description": "第1阶段完成 - 检查钻头状态后点击「继续」"
}
```

---

## 8. 故障排查

### 8.1 常见故障代码

| 故障代码 | 说明 | 可能原因 | 解决方法 |
|---------|------|----------|----------|
| `FORCE_UPPER_LIMIT` | 上力传感器超限 | 钻具卡住 / 地层过硬 | 降低进给速度或增大力限制 |
| `FORCE_LOWER_LIMIT` | 下力传感器超限 | 钻具卡住 / 地层过硬 | 降低进给速度或增大力限制 |
| `TORQUE_LIMIT` | 扭矩超限 | 回转阻力过大 | 降低回转速度或增大扭矩限制 |
| `STALL_DETECTED` | 堵转检测 | 进给速度过慢 | 检查钻具磨损或降低负载 |
| `MAX_FEED_SPEED` | 速度超限 | 控制器参数异常 | 检查Fz机构参数配置 |
| `POSITION_DEVIATION` | 位置偏差过大 | 编码器故障 / 机械卡滞 | 检查Fz机构机械状态 |

### 8.2 调试建议

**首次测试新任务：**
1. 降低所有速度参数到50%（`vp_mm_per_min` 和 `rpm` 减半）
2. 增大超时时间（`timeout` 翻倍）
3. 在每个阶段后插入 `hold` 步骤进行人工检查
4. 观察传感器数据趋势，调整安全阈值

**现场测试检查清单：**
- [ ] 确保数据采集系统已启动（SensorPage 显示绿色指示灯）
- [ ] 检查 Modbus 连接状态（MdbPage 显示绿色）
- [ ] 检查电机连接状态（ControlPage 显示绿色）
- [ ] 执行前手动测试 Fz/Pr/Pi 机构响应性
- [ ] 准备好急停按钮
- [ ] 实时监控力传感器和扭矩数据

---

## 9. 最佳实践

### 9.1 参数调整策略

**钻进速度调整：**
```
软地层（土壤、砂层）：
  vp_mm_per_min = 50-70
  rpm = 30-60

中等地层（粘土、泥岩）：
  vp_mm_per_min = 30-50
  rpm = 60-90

硬地层（砂岩、石灰岩）：
  vp_mm_per_min = 20-30
  rpm = 90-120
  fi_hz = 3-5（可选开启冲击）
```

**安全阈值调整：**
```
保守策略（首次测试）：
  upper_force_limit = 500-600N
  torque_limit_nm = 800-1000N·m

标准策略（正常作业）：
  upper_force_limit = 600-700N
  torque_limit_nm = 1000-1200N·m

激进策略（硬地层）：
  upper_force_limit = 700-800N
  torque_limit_nm = 1200-1600N·m
```

### 9.2 阶段划分建议

**按深度划分（推荐）：**
```
0-300mm   → 低速探测阶段（30rpm）
300-600mm → 中速稳定阶段（60rpm）
600mm+    → 高速钻进阶段（90rpm）
```

**按地层类型划分：**
```
软地层   → P_DRILL_SOFT（50mm/min, 30rpm）
中等地层 → P_DRILL_MEDIUM（30mm/min, 60rpm）
硬地层   → P_DRILL_HARD（20mm/min, 90rpm, 5Hz冲击）
```

### 9.3 配置文件管理

**版本控制：**
- 每次重大修改后，复制一份带日期的备份：`task_standard_drilling_20250126.json`
- 在 `notes` 字段中记录修改历史

**命名规范：**
```
task_standard_drilling.json     // 标准3阶段钻进
task_test_simple_drilling.json  // 测试任务
task_hard_formation.json        // 硬地层专用
task_soft_formation.json        // 软地层专用
```

**参数预设命名：**
```
P_IDLE          // 空载移动
P_DRILL_STAGE1  // 钻进第1阶段
P_DRILL_STAGE2  // 钻进第2阶段
P_DRILL_SOFT    // 软地层钻进
P_DRILL_HARD    // 硬地层钻进
```

---

## 10. 未来改进计划

### 10.1 位置引用系统 ✅ 已实现（v1.1）

**实现状态：** 已在 v1.1 版本完成

**功能说明：**
- ✅ 使用 `@` 前缀引用位置（如 `"target_depth": "@H"`）
- ✅ 支持任务文件定义 `positions` 字典存储任务特定位置
- ✅ 优先级：任务positions → mechanisms.json key_positions
- ✅ 加载任务时立即验证所有位置引用

**使用示例：**
```json
{
  "positions": {
    "STAGE1_END": 617.0
  },
  "steps": [
    {
      "type": "positioning",
      "target_depth": "@H"  // 引用 mechanisms.json 中的 H 位置
    },
    {
      "type": "drilling",
      "target_depth": "@STAGE1_END"  // 引用任务文件的 positions
    },
    {
      "type": "drilling",
      "target_depth": 500.0  // 仍支持硬编码数值
    }
  ]
}
```

---

### 10.2 参数预设继承

**当前问题：**
- 每个预设都有20+个参数
- 大量参数重复（如 `drill_string_weight_n` 始终为380）

**改进方案：**
```json
{
  "presets": {
    "P_BASE": {
      "drill_string_weight_n": 380.0,
      "dead_zone_width_n": 100.0,
      // ... 公共参数
    },
    "P_DRILL_STAGE1": {
      "extends": "P_BASE",  // 继承基础参数
      "vp_mm_per_min": 30.0,
      "rpm": 30.0,
      // ... 仅覆盖需要的参数
    }
  }
}
```

### 10.3 UI可视化配置

**计划功能：**
- 在 AutoTaskPage 中添加「编辑任务」按钮
- 可视化编辑器：
  - 位置选择下拉框（引用key_positions）
  - 参数滑块调节
  - 步骤拖拽排序
  - 实时JSON预览
- 保存后自动重新加载任务

---

## 11. 附录

### 11.1 其他机构关键位置

#### Sr（存储机构）- 7个钻杆存储位

| 位置键 | 脉冲值 | 角度 (°) | 说明 |
|--------|---------|----------|------|
| A | 0 | 0.0 | 存储位1（基准） |
| B | 30400 | 51.4 | 存储位2 |
| C | 60800 | 102.9 | 存储位3 |
| D | 91200 | 154.3 | 存储位4 |
| E | 121600 | 205.7 | 存储位5 |
| F | 152000 | 257.1 | 存储位6 |
| G | 182400 | 308.6 | 存储位7 |

#### Pr（回转）- 速度控制

| 位置键 | 速度 (rpm) | 说明 |
|--------|-----------|------|
| A | 0 | 停止 |
| B | 30.0 | 正转30rpm |
| C | -30.0 | 反转30rpm |
| D | 60.0 | 正转60rpm |

#### Pi（冲击）- 频率控制

| 位置键 | 频率 (Hz) | 说明 |
|--------|----------|------|
| A | 0 | 停止 |
| B | 5.0 | 冲击频率5Hz |

### 11.2 单位转换参考

| 机构 | 参数 | 转换系数 | 公式 |
|------|------|----------|------|
| Fz | 位置 | 13086.9 脉冲/mm | 脉冲 = mm × 13086.9 |
| Sr | 位置 | 591.11 脉冲/度 | 脉冲 = 角度 × 591.11 |
| Mr | 位置 | 144.44 脉冲/度 | 脉冲 = 角度 × 144.44 |
| Pr | 速度 | 直接使用rpm | 无需转换 |
| Pi | 频率 | 直接使用Hz | 无需转换 |

### 11.3 参考文档

- `MECHANISM_CONTROLLERS_GUIDE.md` - 机构控制器详细规范
- `SAFETY_WATCHDOG_MIGRATION.md` - SafetyWatchdog迁移说明
- `AUTO_TASK_USER_GUIDE.md` - 自动任务系统用户指南
- `KEY_POSITIONS_GUIDE.md` - 关键位置定义指南

---

**文档版本：** 1.1
**最后更新：** 2025-01-26（添加位置引用功能）
**作者：** DrillControl开发团队

---

## 更新历史

**v1.1 (2025-01-26):**
- ✅ 实现位置引用系统（`@` 前缀语法）
- ✅ 支持任务文件定义 `positions` 字典
- ✅ 更新所有示例使用新的位置引用语法

**v1.0 (2025-01-26):**
- 初始版本，记录自动任务配置系统的完整规范
