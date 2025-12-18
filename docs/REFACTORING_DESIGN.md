# ControlPage 重构设计文档

## 设计目标

从原有的6786行单体zmotionpage重构为模块化、可维护、可扩展的架构。

## 核心设计原则

1. **SOLID原则**
   - Single Responsibility: 每个类只负责一个功能模块
   - Open/Closed: 对扩展开放，对修改关闭
   - Liskov Substitution: 使用接口和抽象基类
   - Interface Segregation: 接口细粒度化
   - Dependency Inversion: 依赖抽象而非具体实现

2. **DRY原则** - 避免代码重复，提取通用逻辑

3. **KISS原则** - 保持简单，不过度设计

## 模块划分

### 1. 硬件抽象层 (Hardware Abstraction Layer)

#### IMotionDriver (接口)
```cpp
class IMotionDriver {
public:
    virtual bool setAxisEnable(int axis, bool enable) = 0;
    virtual bool setPosition(int axis, double position) = 0;
    virtual bool setSpeed(int axis, double speed) = 0;
    virtual bool setDAC(int axis, double dac) = 0;
    virtual bool setAxisType(int axis, int type) = 0;
    virtual double getPosition(int axis) = 0;
    virtual double getSpeed(int axis) = 0;
    // ... 其他基础操作
};
```

#### ZMotionDriver (ZMotion实现)
- 封装所有ZAux_*函数调用
- 处理错误和异常
- 提供统一的返回值和错误码

#### ModbusDriver (Modbus实现)
- 封装Modbus TCP通信
- 支持推杆等外部设备控制

### 2. 机构控制器层 (Mechanism Controllers)

#### BaseMechanismController (抽象基类)
所有机构控制器的基类，提供通用功能：
```cpp
class BaseMechanismController : public QObject {
    Q_OBJECT
protected:
    IMotionDriver* m_driver;
    MechanismConfig m_config;
    MechanismState m_state;
    
public:
    virtual bool initialize() = 0;
    virtual bool stop() = 0;
    virtual bool reset() = 0;
    virtual bool isReady() const = 0;
    
    // 状态查询
    MechanismState state() const { return m_state; }
    QString stateString() const;
    
signals:
    void stateChanged(MechanismState newState);
    void errorOccurred(const QString& error);
    void progressUpdated(int percent, const QString& message);
};
```

#### 具体机构控制器

**RoboticArmController** - 机械手控制
```cpp
class RoboticArmController : public BaseMechanismController {
    Q_OBJECT
public:
    // 旋转控制
    bool setRotationAngle(double angle);
    bool rotateToPosition(RobotPosition position); // DRILL/STORAGE
    double currentAngle() const;
    
    // 伸缩控制
    bool setExtensionLength(double length);
    bool extend();
    bool retract();
    double currentLength() const;
    
    // 夹爪控制
    bool openClamp();
    bool closeClamp(double torque);
    ClampState clampState() const;
    
    // 初始化
    bool initializeClamp();
    bool initializeExtension();
    
private:
    RotationController m_rotation;
    ExtensionController m_extension;
    ClampController m_clamp;
};
```

**PenetrationController** - 进给机构控制
```cpp
class PenetrationController : public BaseMechanismController {
    Q_OBJECT
public:
    // 深度控制
    bool setTargetDepth(double depthMm);
    bool startPenetration(double speed);
    bool stopPenetration();
    bool gotoSafePosition();
    
    // 深度查询
    double currentDepth() const;
    double targetDepth() const;
    
    // 安全限位
    bool setDepthLimits(double minMm, double maxMm);
    DepthLimits depthLimits() const;
    
private:
    // 脉冲与毫米转换
    double pulsesToMm(double pulses) const;
    double mmToPulses(double mm) const;
    
    double m_offsetPulses = 0.0;
    const double PULSE_PER_MM = 13086.9;
};
```

**DrillController** - 钻进控制（旋转+冲击）
```cpp
class DrillController : public BaseMechanismController {
    Q_OBJECT
public:
    // 旋转控制
    bool startRotation(double rpm);
    bool stopRotation();
    bool setRotationSpeed(double rpm);
    
    // 冲击控制
    bool startPercussion(double frequency);
    bool stopPercussion();
    bool setPercussionFrequency(double hz);
    
    // 锁定控制
    bool unlockPercussion();
    bool lockPercussion();
    bool isPercussionLocked() const;
    
    // 一键对接/断开
    bool quickConnect();
    bool quickDisconnect();
    
private:
    RotationMotor m_rotationMotor;
    PercussionMotor m_percussionMotor;
    QStateMachine m_stateMachine;
};
```

**StorageController** - 存储机构控制
```cpp
class StorageController : public BaseMechanismController {
    Q_OBJECT
public:
    bool moveForward();
    bool moveBackward();
    bool moveToPosition(int position); // 0-6
    int currentPosition() const;
    
private:
    int m_currentPosition = 0;
    double m_offsetAngle = 0.0;
    const int POSITIONS = 7;
    const double ANGLE_PER_POSITION = 51.43; // 360/7
};
```

**ClampController** - 夹紧机构控制
```cpp
class ClampController : public BaseMechanismController {
    Q_OBJECT
public:
    bool open();
    bool close(double torque);
    bool initialize();
    ClampState state() const;
    
private:
    void startTorqueMode(double dac);
    void lockPosition();
    bool waitForStable(int timeoutMs);
};
```

### 3. 状态管理系统

#### MechanismState (状态枚举)
```cpp
enum class MechanismState {
    Uninitialized,
    Initializing,
    Ready,
    Moving,
    Error,
    EmergencyStop
};
```

#### StateMachine (状态机)
使用Qt的QStateMachine来管理复杂状态转换：
- 初始化状态机
- 运动状态机
- 错误恢复状态机

### 4. 配置管理系统

#### MechanismConfig
```cpp
struct MechanismConfig {
    int motorId;
    double defaultSpeed;
    double acceleration;
    double deceleration;
    double maxSpeed;
    double minSpeed;
    
    // 从JSON加载
    static MechanismConfig fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
};
```

#### ConfigManager
```cpp
class ConfigManager : public QObject {
    Q_OBJECT
public:
    bool loadFromFile(const QString& filePath);
    bool saveToFile(const QString& filePath);
    
    MechanismConfig getConfig(const QString& mechanismName) const;
    bool setConfig(const QString& mechanismName, const MechanismConfig& config);
    
signals:
    void configChanged(const QString& mechanismName);
};
```

### 5. 任务引擎系统

#### TaskEngine
```cpp
class TaskEngine : public QObject {
    Q_OBJECT
public:
    bool loadScript(const QString& scriptPath);
    bool startExecution();
    bool pauseExecution();
    bool stopExecution();
    bool stepNext();
    
    ExecutionState state() const;
    
signals:
    void progressUpdated(int step, int total, const QString& message);
    void executionFinished(bool success);
    void errorOccurred(const QString& error);
    
private:
    QList<TaskStep> m_steps;
    int m_currentStep = 0;
    ExecutionState m_state;
};
```

#### TaskStep
```cpp
struct TaskStep {
    QString mechanismName;
    QString action;
    QVariantMap parameters;
    int timeoutMs;
    bool waitForCompletion;
    
    // 脚本格式示例 (JSON):
    // {
    //   "mechanism": "roboticArm",
    //   "action": "rotateToPosition",
    //   "parameters": {"position": "DRILL"},
    //   "timeout": 5000,
    //   "wait": true
    // }
};
```

### 6. UI层重构

#### ControlPage (主控制页面)
```cpp
class ControlPage : public QWidget {
    Q_OBJECT
public:
    void setAcquisitionManager(AcquisitionManager* manager);
    
private:
    // UI组件
    ConnectionPanel* m_connectionPanel;
    MotorTableWidget* m_motorTable;
    
    // 机构控制面板
    RoboticArmPanel* m_roboticArmPanel;
    PenetrationPanel* m_penetrationPanel;
    DrillPanel* m_drillPanel;
    StoragePanel* m_storagePanel;
    ClampPanel* m_clampPanel;
    
    // 控制器
    RoboticArmController* m_roboticArm;
    PenetrationController* m_penetration;
    DrillController* m_drill;
    StorageController* m_storage;
    ClampController* m_clamp;
    
    // 支持系统
    ConfigManager* m_configManager;
    TaskEngine* m_taskEngine;
};
```

#### 机构控制面板（独立Widget）
每个机构的UI封装为独立Widget：
- RoboticArmPanel - 机械手控制面板
- PenetrationPanel - 进给控制面板
- DrillPanel - 钻进控制面板
- StoragePanel - 存储机构面板
- ClampPanel - 夹紧控制面板

优点：
- UI模块化，便于维护和测试
- 可以独立开发和调试
- 可以在不同页面复用
- 便于实现响应式布局

## 实施计划

### 阶段1: 基础设施 (1-2天)
1. 创建IMotionDriver接口
2. 实现ZMotionDriver
3. 创建BaseMechanismController
4. 实现ConfigManager
5. 定义所有数据结构和枚举

### 阶段2: 核心控制器 (3-4天)
1. 实现RoboticArmController
2. 实现PenetrationController
3. 实现DrillController
4. 实现StorageController
5. 实现ClampController

每个控制器包括：
- 基础功能实现
- 初始化逻辑
- 状态管理
- 单元测试

### 阶段3: UI重构 (2-3天)
1. 设计新的UI布局
2. 实现各机构控制面板
3. 实现ControlPage集成
4. 美化和优化

### 阶段4: 高级功能 (2-3天)
1. 实现TaskEngine
2. 实现自动化模式
3. 实现一键对接等复合操作
4. 实现数据记录和回放

### 阶段5: 测试和优化 (1-2天)
1. 集成测试
2. 性能优化
3. 文档完善
4. Bug修复

## 技术亮点

1. **可测试性**：通过接口抽象，可以mock硬件进行单元测试
2. **可维护性**：模块化设计，修改一个模块不影响其他模块
3. **可扩展性**：添加新机构只需继承BaseMechanismController
4. **配置驱动**：运行时加载配置，无需重新编译
5. **类型安全**：使用enum class避免魔法数字
6. **线程安全**：使用Qt的信号槽机制保证线程安全
7. **错误处理**：统一的错误处理和上报机制
8. **状态管理**：清晰的状态机，便于调试和追踪

## 对比原设计的优势

| 方面 | 原设计 | 新设计 |
|------|--------|--------|
| 代码行数 | 6786行单文件 | 分散到多个小文件，每个<500行 |
| 耦合度 | 高度耦合 | 松耦合，基于接口 |
| 可测试性 | 难以测试 | 易于单元测试 |
| 可维护性 | 难以维护 | 模块化，易于维护 |
| 状态管理 | 40+散乱变量 | 统一状态机管理 |
| 配置方式 | 硬编码常量 | JSON配置驱动 |
| 错误处理 | 分散各处 | 统一异常和信号 |
| UI组织 | 单页2363行UI | 模块化组件 |

## 下一步行动

请确认这个重构方案是否符合您的期望，然后我们开始逐步实现。
