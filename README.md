# DrillControl 钻机采集控制系统 v2.0
## Recent Updates (2025-12-22)
- Added a physical-unit toggle (mm/min or deg/min) for the ControlPage motor table; driver units shown in the status line.
- Added `config/unit_conversions.csv` with optional `pulses_per_rev`, `reduction_ratio`, and `mm_per_rev` fields.
- Added auto duration estimation in PlanVisualizer using `config/plan_step_map.json` and mechanism parameters.
- Auto-computed durations are saved to `config/durations.json`.
- Plan step mapping supports parallel ops per step (duration uses the slowest move).
- Added default duration auto-load when `--dur_config` is omitted in `python/multi_rig_plan/scheduler.py` and `python/multi_rig_plan/serial_autoload.py`.

## Feature Overview
- Data acquisition: VK701 vibration (high-rate), Modbus TCP sensors, and ZMotion motor telemetry with independent sample rates and aligned timestamps.
- Round management: start/stop all, new/reset/end rounds, round notes, and AutoTask-triggered round creation.
- Motion control UI: EtherCAT bus init, per-axis enable/stop/zero/clear alarm, realtime parameter table, command console, and driver/physical unit toggle.
- Motion controllers: feed (Fz), rotation (Pr), percussion (Pi), arm extension/grip/rotation (Me/Mg/Mr), docking head (Dh), storage (Sr), and clamp (Cb).
- AutoTask: JSON task load/import, presets, state machine execution, step progress, pause/resume/stop/emergency, and logging.
- Planning & visualization: Python serial/scheduler planners, duration table with auto-compute, Gantt chart, ASCII timeline, and JSON export.
- Database & query: SQLite round storage, scalar/vibration blocks, time-window queries, and plots/export.
- Database path is shared between acquisition and queries (set at startup by AcquisitionManager).

## Configuration Files
- `config/mechanisms.json`: mechanism parameters, key positions, limits, and control modes.
- `config/unit_conversions.csv`: driver/physical conversion (pulses_per_unit or pulses_per_rev + reduction_ratio [+ mm_per_rev]).
- `config/plan_step_map.json`: plan step to axis mapping (supports parallel ops per step).
- `config/durations.json`: computed plan durations used by planners and UI.
- `config/auto_tasks/`: AutoTask JSON tasks and presets (user-provided).
- `python/multi_rig_plan/`: planning scripts (`serial.py`, `serial_autoload.py`, `scheduler.py`) and overrides.

## Recent Fixes (2025-12-21)
- Fixed MDB/Motor sampling timers so QTimer timeouts are delivered.
- Added event-loop-based Modbus connect/disconnect waits with timeouts.
- Ensured auto-task reconnects data workers after controllers are set.
- Scoped time-window cache by round to avoid cross-round window reuse.
- Details: docs/BUGFIX_20251221_Acquisition_AutoTask.md


## 项目简介

这是一个基于Qt5的钻机数据采集与控制上位机软件，采用简洁的模块化设计。

## 项目结构

```
DrillControl/
├── DrillControl.pro              # Qt项目文件
├── src/                          # 源代码目录
│   ├── main.cpp                  # 程序入口
│   ├── Global.cpp                # 全局变量定义
│   ├── ui/                       # 界面层
│   │   ├── MainWindow.cpp        # 主窗口实现
│   │   ├── SensorPage.cpp        # 传感器页面（数据采集）
│   │   ├── VibrationPage.cpp     # 振动监测页面
│   │   ├── MdbPage.cpp           # Modbus监测页面
│   │   ├── MotorPage.cpp         # 电机页面
│   │   ├── ControlPage.cpp       # 基础运动控制页面
│   │   ├── DatabasePage.cpp      # 数据库管理页面
│   │   └── DrillControlPage.cpp  # 钻机高级控制页面 ⭐ 新增
│   ├── dataACQ/                  # 数据采集层
│   │   ├── BaseWorker.cpp        # 采集工作基类
│   │   ├── VibrationWorker.cpp   # 震动传感器采集
│   │   ├── MdbWorker.cpp         # MDB传感器采集
│   │   └── MotorWorker.cpp       # 电机参数采集
│   ├── database/                 # 数据库层
│   │   └── DbWriter.cpp          # 数据库写入
│   └── control/                  # 控制层
│       ├── ZMotionDriver.cpp     # ZMotion驱动封装
│       ├── AcquisitionManager.cpp # 采集管理器
│       ├── MotionLockManager.cpp # 运动互锁管理器 ⭐ 重构
│       ├── MotionConfigManager.cpp # 配置管理器
│       ├── MechanismTypes.cpp    # 机构类型定义
│       ├── BaseMechanismController.cpp # 机构控制基类
│       ├── FeedController.cpp    # 进给控制器 (Fz) ⭐ 新增
│       ├── RotationController.cpp # 回转控制器 (Pr) ⭐ 新增
│       ├── PercussionController.cpp # 冲击控制器 (Pi) ⭐ 新增
│       ├── ArmExtensionController.cpp # 机械手伸缩 (Me) ⭐ 新增
│       ├── ArmGripController.cpp # 机械手夹紧 (Mg) ⭐ 新增
│       ├── ArmRotationController.cpp # 机械手回转 (Mr) ⭐ 新增
│       ├── DockingController.cpp # 对接推杆 (Dh) ⭐ 新增
│       ├── StorageController.cpp # 料仓控制器 (Sr)
│       └── ClampController.cpp   # 夹紧控制器 (Cb)
├── include/                      # 头文件目录
│   ├── Global.h                  # 全局变量和映射表
│   ├── ui/                       # 界面头文件
│   │   ├── MainWindow.h
│   │   ├── SensorPage.h
│   │   ├── VibrationPage.h
│   │   ├── MdbPage.h
│   │   ├── MotorPage.h
│   │   ├── ControlPage.h         # 基础运动控制页面
│   │   ├── DatabasePage.h
│   │   └── DrillControlPage.h    # 钻机高级控制页面 ⭐ 新增
│   ├── dataACQ/                  # 数据采集头文件
│   │   ├── DataTypes.h
│   │   ├── BaseWorker.h
│   │   ├── VibrationWorker.h
│   │   ├── MdbWorker.h
│   │   └── MotorWorker.h
│   ├── database/                 # 数据库头文件
│   │   └── DbWriter.h
│   └── control/                  # 控制头文件
│       ├── zmotion.h             # ZMotion API
│       ├── zmcaux.h              # ZMotion辅助函数
│       ├── IMotionDriver.h       # 运动驱动接口
│       ├── ZMotionDriver.h
│       ├── AcquisitionManager.h
│       ├── MotionLockManager.h   # 运动互锁管理器 ⭐ 重构
│       ├── MotionConfigManager.h # 配置管理器
│       ├── MechanismDefs.h       # 机构代号定义 ⭐ 新增
│       ├── MechanismTypes.h
│       ├── BaseMechanismController.h
│       ├── FeedController.h      # 进给控制器 (Fz) ⭐ 新增
│       ├── RotationController.h  # 回转控制器 (Pr) ⭐ 新增
│       ├── PercussionController.h # 冲击控制器 (Pi) ⭐ 新增
│       ├── ArmExtensionController.h # 机械手伸缩 (Me) ⭐ 新增
│       ├── ArmGripController.h   # 机械手夹紧 (Mg) ⭐ 新增
│       ├── ArmRotationController.h # 机械手回转 (Mr) ⭐ 新增
│       ├── DockingController.h   # 对接推杆 (Dh) ⭐ 新增
│       ├── StorageController.h   # 钻管仓转位控制器 (Sr)
│       └── ClampController.h     # 夹紧控制器 (Cb)
├── forms/                        # Qt UI文件
│   ├── MainWindow.ui             # 主窗口界面
│   ├── SensorPage.ui             # 传感器页面界面
│   ├── VibrationPage.ui          # 振动监测界面
│   ├── MdbPage.ui                # Modbus监测界面
│   ├── MotorPage.ui              # 电机页面界面
│   ├── ControlPage.ui            # 基础运动控制界面
│   ├── DatabasePage.ui           # 数据库管理界面
│   └── DrillControlPage.ui       # 钻机高级控制界面 ⭐ 新增
├── docs/                         # 文档目录
│   ├── CONTROL_PAGE_MIGRATION_REPORT.md  # ControlPage迁移报告
│   ├── DRILLCONTROLPAGE_IMPL.md  # DrillControlPage实现记录
│   ├── MOTION_INTERLOCK_SYSTEM.md # 运动互锁系统设计文档
│   ├── MECHANISM_CONTROLLERS_GUIDE.md # 机构控制器详细指南
│   ├── MECHANISM_OPERATION_LOGIC.md # 机构操作逻辑详解 ⭐ 新增
│   ├── UPDATES_20250125.md       # 最新更新说明
│   └── ...                       # 其他文档
├── thirdparty/                   # 第三方库
│   ├── qcustomplot/              # 图表库
│   │   ├── include/
│   │   └── src/
│   ├── vk701/                    # VK701采集卡
│   │   ├── include/
│   │   └── lib/
│   ├── zmotion/                  # ZMotion运动控制
│   │   ├── include/
│   │   └── lib/
│   └── sqlite3/                  # SQLite数据库
│       ├── include/
│       └── lib/
├── build/                        # 构建输出目录
│   └── debug/
├── python/                       # Python脚本（预留）
├── config/                       # 配置文件
│   └── mechanisms.json           # 机构运动参数配置 ⭐ 新增
└── database/                     # 数据库文件（预留）

```

## 功能模块

### 1. 数据采集模块（待实现）

- **震动传感器采集**: VK701采集卡，3通道，5000Hz可调
- **MDB数据采集**: 上下拉力、位置、扭矩传感器，10Hz
- **电机参数读取**: 三环参数读取

### 2. 界面功能

- **多页面管理**: 侧边栏标签切换，支持窗口弹出
- **工业级布局**: 简洁的UI设计
- **子界面**:
  - 数据采集页面 (SensorPage)
  - 振动监测页面 (VibrationPage)
  - Modbus监测页面 (MdbPage)
  - 电机参数页面 (MotorPage)
  - 基础运动控制页面 (ControlPage)
  - 数据库管理页面 (DatabasePage)
  - 钻机高级控制页面 (DrillControlPage) ⭐ 新增

### 3. 控制功能 ✅ (ControlPage - 已完成)

**运动控制中心** - 完全重构自原项目zmotionpage，代码量减少91%

#### 核心功能
- **总线初始化**: EtherCAT总线初始化与状态监控
- **电机参数表格**: 10轴电机参数实时显示与编辑
  - EN (使能)、MPos (实际位置)、Pos (指令位置)
  - MVel (实际速度)、Vel (指令速度)、DAC (输出)
  - Atype (轴类型)、Unit (脉冲当量)、Acc/Dec (加减速度)
- **表格编辑功能**:
  - ✅ 双击单元格编辑参数
  - ✅ 位置编辑触发电机运动 (MoveAbs/Move)
  - ✅ DAC设置自动检查轴类型 (力矩/速度模式)
  - ✅ 只读列保护 (MPos, MVel)
  - ✅ 数值验证与旧值恢复
- **单轴详细控制**:
  - 轴参数详情查看（位置、速度、加速度等）
  - 伺服使能/禁用控制
  - **清除报警** 🆕: `ZAux_BusCmd_DriveClear()`
  - **设置零点** 🆕: `ZAux_Direct_SetMpos(0)`
  - 自动轮询更新
- **绝对/相对运动模式** 🆕:
  - 勾选"绝对位置模式": 使用 `MoveAbs` (绝对运动)
  - 取消勾选: 使用 `Move` (相对运动)
- **命令终端**:
  - 直接发送ZMotion BASIC命令
  - 特殊命令 `?Map` 查看电机映射 🆕
- **实时监控**: 100ms高频刷新，500ms轴信息更新
- **紧急停止**: 一键停止所有电机

#### 技术亮点
- 代码量: 6786行 → 610行 (减少91%)
- 完整错误处理与用户反馈
- 现代工业风格UI
- 模块化设计，易于维护

详细信息请参阅: `docs/CONTROL_PAGE_MIGRATION_REPORT.md`

### 4. 钻机高级控制 ✅ (DrillControlPage - 新增)

**机构独立控制** - 9个自由度机构的手动控制界面

#### 核心功能
- **JSON配置管理**: `config/mechanisms.json` 统一管理机构参数
  - 支持热更新（QFileSystemWatcher）
  - 包含电机ID、速度、加减速、限位等参数
- **9个机构独立控制**:
  - **Fz (进给)**: 深度控制、安全位置
  - **Pr (回转)**: 转速控制、力矩模式
  - **Pi (冲击)**: 解锁/锁定、频率控制
  - **Cb (夹紧)**: 开/闭、力矩控制
  - **Sr (料仓)**: 7位离散位置控制
  - **Dh (对接推杆)**: Modbus TCP控制、伸出/收回
  - **Me (机械手伸缩)**: 位置控制、预设位置
  - **Mg (机械手夹紧)**: 开/闭、力矩控制
  - **Mr (机械手回转)**: 角度控制、钻进/料仓位
- **3x3网格布局**: 清晰的机构分组显示
- **实时状态监控**: 200ms刷新，显示位置、速度、状态
- **统一初始化与停止**: 一键初始化/停止所有机构
- **操作日志**: 实时显示操作记录

#### 架构特点
- 每个自由度独立控制器，继承 `BaseMechanismController`
- `MotionConfigManager` 单例管理JSON配置
- 支持EtherCAT和Modbus TCP混合控制
- 机构代号映射系统（Fz/Pr/Pi/Cb/Sr/Me/Mg/Mr/Dh）

详细信息请参阅: `docs/DRILLCONTROLPAGE_IMPL.md`

### 5. 数据库功能（待实现）

- SQLite3异步批量写入
- 数据查询与管理界面

## 技术特点

- **多线程数据采集**: 不阻塞主控制线程
- **异步数据库写入**: 高频数据采集性能优化
- **运动互锁机制**: 手动/自动运动安全互锁，冲突弹窗确认 ⭐ 新增
- **单一句柄架构**: 全局互斥锁保护，线程安全 ⭐ 新增
- **模块化设计**: 清晰的目录结构和职责分离
- **Python集成支持**: 预留Python脚本接口

## 现场调试与测试

- 现场测试的准备、执行顺序、数据记录模板详见 `docs/ONSITE_TESTING_PLAN.md`，务必遵循"低风险 → 高风险 → 数据采集/安全"的顺序推进。
- 每个动作完成后立即导出 `logs/` 与 SQLite 数据库，连同驱动面板/传感器截图一并回填测试表，方便追溯。
- 使用 DrillControlPage 状态栏监视 MotionLockManager，若出现 "Busy" 必须在动作前解除并记录责任人及原因。

## 下位机 ZMotion 程序

- ZMotion BASIC 源码位于 `ZmotionCode/Zbasic_/ECAT_Scan.bas`，负责 EtherCAT 扫描、节点映射与 PDO/轴参数初始化。
- 通过 ZDevelop 或 ControlPage 终端运行 `ECAT_Scan.bas` 后，在 Command Terminal 输入 `?Map` 并截图，确认轴号、厂商号与 `config/mechanisms.json` 一致。
- 优化后的代码包含统一的日志函数 `LogStage()`，会输出详细的节点信息、轴映射表、使能状态等，便于与上位机日志对齐。

### 线程架构 ⭐ 新增

| 线程 | 用途 | 频率 | 说明 |
|------|------|------|------|
| 主线程 | UI + 运动控制 | - | 运动互锁管理 |
| VibrationThread | VK701震动采集 | 5000Hz | 独立采集 |
| MdbThread | Modbus传感器采集 | 10Hz | 独立采集 |
| MotorThread | 电机参数采集 | 100Hz | 只读访问 |
| DbThread | 数据库写入 | 异步 | 批量写入 |

详细信息请参阅: `docs/MOTION_INTERLOCK_SYSTEM.md`

## 开发环境

- **Qt版本**: Qt 5.15.2
- **编译器**: MSVC 2019 / MinGW 8.1
- **C++标准**: C++17
- **操作系统**: Windows 10/11

## 编译说明

### 使用Qt Creator

1. 打开 `DrillControl.pro`
2. 配置构建工具（MSVC或MinGW）
3. 点击构建按钮

### 使用命令行

```bash
# 生成Makefile
qmake DrillControl.pro

# 编译
nmake          # MSVC
mingw32-make   # MinGW

# 运行
cd build/debug
DrillControl.exe
```

## 第三方库依赖

### QCustomPlot
- 用途: 实时数据绘图
- 版本: 2.x
- 位置: `thirdparty/qcustomplot/`

### VK701采集卡
- 用途: 震动传感器数据采集
- 位置: `thirdparty/vk701/`
- DLL: `VK70xNMC_DAQ2.dll`

### ZMotion
- 用途: 运动控制
- 位置: `thirdparty/zmotion/`
- DLL: `zmotion.dll`, `zauxdll.dll`

### SQLite3
- 用途: 本地数据库
- 位置: `thirdparty/sqlite3/`
- DLL: `sqlite3.dll`

## 待实现功能

- [x] 震动传感器采集模块 ✅
- [x] MDB传感器采集模块 ✅
- [x] 运动控制模块 ✅
- [x] 电机参数表格 ✅
- [x] 钻机高级控制（9个机构独立控制） ✅
- [x] 运动互锁系统 ✅ ⭐ 新增
- [ ] 数据库管理器
- [ ] 数据查询界面
- [ ] 数据可视化图表
- [ ] 自动化钻进流程
- [ ] Python脚本集成

## 项目特色

### 相比旧版本的改进

1. **目录结构简洁**: 按功能模块清晰分层
2. **第三方库集中管理**: 所有依赖统一放在 thirdparty 目录
3. **易于维护**: 模块化设计，职责明确
4. **JSON配置管理**: 机构参数集中配置，支持热更新
5. **遵循KISS原则**: 简单可维护，不过度设计
6. **ControlPage重构** ⭐:
   - 代码量从6786行减少到610行 (减少91%)
   - 完整的错误处理和用户反馈
   - 现代工业风格UI
   - 表格编辑功能完全修复（位置触发运动、DAC检查）
   - 新增清除报警、设置零点、绝对/相对运动模式
7. **DrillControlPage** ⭐ 新增:
   - 9个机构独立控制器，每个自由度独立封装
   - 统一的BaseMechanismController基类
   - JSON配置文件管理所有机构参数
   - 支持EtherCAT和Modbus TCP混合控制
   - 3x3网格清晰布局
8. **运动互锁系统** ⭐ 新增:
   - 单一句柄架构，消除竞争风险
   - 全局互斥锁保护所有ZMotion API调用
   - 手动/自动运动可互相打断，冲突弹窗确认
   - 急停功能无条件立即停止
   - 数据采集只读访问，不影响运动控制

## 开发计划

### 第一阶段: 基础框架 ✅
- [x] 项目目录结构
- [x] 主窗口框架
- [x] 页面切换机制

### 第二阶段: 数据采集 ✅
- [x] VK701震动采集
- [x] MDB传感器采集
- [x] 传感器页面UI

### 第三阶段: 数据库
- [ ] SQLite数据库封装
- [ ] 异步写入机制
- [ ] 数据查询界面

### 第四阶段: 运动控制 ✅ (完成)
- [x] ZMotion运动控制封装
- [x] ControlPage控制界面开发
- [x] 电机参数表格显示与编辑
- [x] 10轴电机实时监控
- [x] 单轴详细控制
- [x] 清除报警与设置零点
- [x] 绝对/相对运动模式
- [x] DrillControlPage钻机高级控制
- [x] 9个机构独立控制器（Fz/Pr/Pi/Cb/Sr/Me/Mg/Mr/Dh）
- [x] JSON配置管理器（MotionConfigManager）
- [x] EtherCAT + Modbus TCP混合控制
- [x] 运动互锁系统（MotionLockManager） ⭐ 新增
- [x] 单一句柄架构 + 全局互斥锁
- [x] 手动/自动运动冲突弹窗确认

### 第五阶段: 自动化控制
- [ ] 钻进流程编排
- [ ] 取样自动化流程
- [ ] 故障检测与处理
- [ ] 远程监控接口
