# DrillControlPage 实现记录

## 概述
新增"钻机高级控制"页面，提供9个机构的独立手动控制界面。

## 实现日期
2025-01-25

## 机构代号与电机映射

| 机构代号 | 名称 | 电机ID | 连接方式 | 控制器类 |
|---------|------|--------|---------|---------|
| Fz | 进给 | 2 | EtherCAT | FeedController |
| Pr | 回转 | 0 | EtherCAT | RotationController |
| Pi | 冲击 | 1 | EtherCAT | PercussionController |
| Cb | 夹紧 | 3 | EtherCAT | ClampController |
| Sr | 料仓 | 7 | EtherCAT | StorageController |
| Me | 机械手伸缩 | 6 | EtherCAT | ArmExtensionController |
| Mg | 机械手夹紧 | 4 | EtherCAT | ArmGripController |
| Mr | 机械手回转 | 5 | EtherCAT | ArmRotationController |
| Dh | 对接推杆 | - | Modbus TCP | DockingController |

## 新增文件

### 控制器 (9个)
- `FeedController.h/.cpp` - 进给控制器 (Fz)
- `RotationController.h/.cpp` - 回转控制器 (Pr)
- `PercussionController.h/.cpp` - 冲击控制器 (Pi)
- `ArmExtensionController.h/.cpp` - 机械手伸缩 (Me)
- `ArmGripController.h/.cpp` - 机械手夹紧 (Mg)
- `ArmRotationController.h/.cpp` - 机械手回转 (Mr)
- `DockingController.h/.cpp` - 对接推杆 (Dh, Modbus)
- `ClampController` - 更新，添加机构代号
- `StorageController` - 更新，添加机构代号

### 配置管理
- `MechanismDefs.h` - 机构代号定义与映射
- `MotionConfigManager.h/.cpp` - JSON配置管理器（单例）
- `config/mechanisms.json` - 机构参数配置文件

### 界面
- `DrillControlPage.h/.cpp` - 控制页面实现
- `DrillControlPage.ui` - 3x3网格布局界面

## 删除的文件（重构清理）
- `PenetrationController.h/.cpp` → 被 FeedController 替代
- `DrillController.h/.cpp` → 被 RotationController + PercussionController 替代
- `RoboticArmController.h/.cpp` → 被 ArmExtensionController + ArmGripController + ArmRotationController 替代

## 修改文件
- `MainWindow.h/.cpp` - 添加DrillControlPage标签页
- `DrillControl.pro` - 添加新文件引用，删除旧文件引用
- `README.md` - 更新项目结构和功能说明

## 架构特点
1. **完全拆分**: 每个自由度独立控制器，继承BaseMechanismController
2. **JSON配置**: MotionConfigManager单例管理配置，支持热更新(QFileSystemWatcher)
3. **混合控制**: 支持EtherCAT（8轴）+ Modbus TCP（1轴对接推杆）
4. **统一接口**: 所有控制器实现 initialize/stop/reset/updateStatus
5. **机构代号**: Mechanism::Code枚举映射到电机ID

## 配置文件结构
`config/mechanisms.json`:
```json
{
  "_version": "1.0",
  "mechanisms": {
    "Fz": { "motor_id": 2, "speed": 100, ... },
    "Pr": { "motor_id": 0, "speed": 60, ... },
    ...
  }
}
```

## 界面布局
3x3网格：
```
[Fz 进给]  [Pr 回转]  [Pi 冲击]
[Cb 夹紧]  [Sr 料仓]  [Dh 对接]
[Me 伸缩]  [Mg 夹紧]  [Mr 回转]
```

每个卡片包含：
- 状态标签（颜色指示）
- 实时数值显示（位置/速度/状态）
- 输入框（目标值设定）
- 控制按钮（初始化/运动/停止）

## 代码统计
- 新增控制器代码：约3000行
- 界面代码：约840行
- 配置管理器：约570行
- UI文件：约300行
