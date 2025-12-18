#ifndef MECHANISMTYPES_H
#define MECHANISMTYPES_H

#include <QString>
#include <QJsonObject>
#include <QMetaType>
#include <QMap>

/**
 * @file MechanismTypes.h
 * @brief 机构控制系统的公共数据类型定义
 * 
 * 包含所有机构控制器使用的枚举、结构体和常量定义
 */

// ============================================================================
// 机构状态枚举
// ============================================================================

/**
 * @brief 机构状态
 */
enum class MechanismState {
    Uninitialized,      // 未初始化
    Initializing,       // 初始化中
    Ready,              // 就绪
    Moving,             // 运动中
    Holding,            // 保持位置
    Error,              // 错误状态
    EmergencyStop       // 紧急停止
};

/**
 * @brief 夹爪状态
 */
enum class ClampState {
    Unknown,            // 未知状态
    Open,               // 张开
    Closed,             // 闭合
    Opening,            // 张开中
    Closing,            // 闭合中
    Error               // 错误
};

/**
 * @brief 机械手位置
 */
enum class RobotPosition {
    Drill,              // 钻机位置（0度）
    Storage,            // 存储位置（90度）
    Custom              // 自定义位置
};

/**
 * @brief 电机控制模式
 */
enum class MotorMode {
    Position = 65,      // 位置模式
    Velocity = 66,      // 速度模式
    Torque = 67         // 力矩模式
};

// ============================================================================
// 配置数据结构
// ============================================================================

/**
 * @brief 电机配置参数
 */
struct MotorConfig {
    int motorId = -1;               // 电机ID
    double defaultSpeed = 100.0;    // 默认速度
    double acceleration = 100.0;    // 加速度
    double deceleration = 100.0;    // 减速度
    double maxSpeed = 1000.0;       // 最大速度
    double minSpeed = 0.0;          // 最小速度
    double maxPosition = 1e6;       // 最大位置
    double minPosition = -1e6;      // 最小位置
    
    // JSON序列化
    static MotorConfig fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
    
    // 默认构造
    MotorConfig() = default;
};

/**
 * @brief 机构配置参数
 */
struct MechanismConfig {
    QString name;                   // 机构名称
    bool enabled = true;            // 是否启用
    int initTimeout = 10000;        // 初始化超时(ms)
    
    // JSON序列化
    static MechanismConfig fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
};

/**
 * @brief 深度限位参数
 */
struct DepthLimits {
    double maxDepthMm = 1059.0;     // 最大深度（顶部位置）
    double minDepthMm = 58.0;       // 最小深度（底部位置）
    double safeDepthMm = 1059.0;    // 安全位置深度
    
    bool isValid() const {
        return maxDepthMm > minDepthMm;
    }
};

/**
 * @brief 机械手配置
 */
struct RoboticArmConfig {
    MotorConfig rotation;           // 旋转电机配置
    MotorConfig extension;          // 伸缩电机配置
    MotorConfig clamp;              // 夹爪电机配置
    
    double drillPositionAngle = 0.0;    // 钻机位置角度
    double storagePositionAngle = 90.0; // 存储位置角度
    double extendPosition = 200.0;      // 伸出位置
    double retractPosition = 0.0;       // 回收位置
    
    double clampOpenDAC = -100.0;       // 夹爪张开力矩
    double clampCloseDAC = 100.0;       // 夹爪闭合力矩
    
    static RoboticArmConfig fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
};

/**
 * @brief 进给机构配置
 */
struct PenetrationConfig {
    MotorConfig motor;                  // 进给电机配置

    DepthLimits depthLimits;            // 深度限位
    double pulsesPerMm = 13086.9;       // 脉冲每毫米
    double maxPulses = 13100000.0;      // 最大脉冲数（顶部）

    // 关键位置 (A-J)
    // A=最底端, B=钻管底端对接结束, C=钻管底端对接开始, D=钻管顶端对接结束,
    // E=钻具顶端对接结束, F=钻管顶端对接开始, G=钻具顶端对接开始, H=最顶端,
    // I=搭载钻管后底部对接结束, J=搭载钻管后顶部对接开始
    QMap<QString, double> keyPositions;

    static PenetrationConfig fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
};

/**
 * @brief 钻进控制配置
 */
struct DrillConfig {
    MotorConfig rotation;               // 旋转电机配置
    MotorConfig percussion;             // 冲击电机配置
    
    double defaultRotationSpeed = 60.0; // 默认旋转速度(rpm)
    double defaultPercussionFreq = 5.0; // 默认冲击频率(Hz)
    
    // 冲击解锁参数
    double unlockDAC = -30.0;           // 解锁力矩
    double unlockPosition = -100.0;     // 解锁位置
    int stableTime = 3000;              // 稳定时间(ms)
    double positionTolerance = 1.0;     // 位置容差
    
    static DrillConfig fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
};

/**
 * @brief 存储机构配置
 */
struct StorageConfig {
    MotorConfig motor;                  // 存储电机配置

    int positions = 7;                  // 位置数量
    double anglePerPosition = 51.43;    // 每个位置角度 (360/7)

    // 关键位置 (A-G): 各存储位的角度/脉冲值
    QMap<QString, double> keyPositions;

    static StorageConfig fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
};

/**
 * @brief 夹紧机构配置
 */
struct ClampConfig {
    MotorConfig motor;                  // 夹爪电机配置

    double openDAC = -100.0;            // 张开力矩
    double closeDAC = 100.0;            // 闭合力矩
    double positionTolerance = 1.0;     // 位置容差
    int stableCount = 5;                // 稳定计数阈值

    // 关键位置 (A=完全张开, B=完全夹紧)
    QMap<QString, double> keyPositions;

    static ClampConfig fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
};

// ============================================================================
// 运动参数结构
// ============================================================================

/**
 * @brief 运动参数
 */
struct MotionParameters {
    double speed = 100.0;
    double acceleration = 100.0;
    double deceleration = 100.0;
    double targetPosition = 0.0;
    
    bool isValid() const {
        return speed > 0 && acceleration > 0 && deceleration > 0;
    }
};

// ============================================================================
// 状态信息结构
// ============================================================================

/**
 * @brief 电机状态信息
 */
struct MotorStatus {
    int motorId = -1;
    bool enabled = false;
    double actualPosition = 0.0;
    double targetPosition = 0.0;
    double actualVelocity = 0.0;
    double targetVelocity = 0.0;
    double dacOutput = 0.0;
    MotorMode mode = MotorMode::Position;
};

/**
 * @brief 机构状态信息
 */
struct MechanismStatus {
    QString mechanismName;
    MechanismState state = MechanismState::Uninitialized;
    QString stateMessage;
    double progress = 0.0;              // 0-100
    bool hasError = false;
    QString errorMessage;
};

// ============================================================================
// 辅助函数
// ============================================================================

/**
 * @brief 状态枚举转字符串
 */
inline QString mechanismStateToString(MechanismState state) {
    switch (state) {
        case MechanismState::Uninitialized: return "Uninitialized";
        case MechanismState::Initializing: return "Initializing";
        case MechanismState::Ready: return "Ready";
        case MechanismState::Moving: return "Moving";
        case MechanismState::Holding: return "Holding";
        case MechanismState::Error: return "Error";
        case MechanismState::EmergencyStop: return "Emergency Stop";
        default: return "Unknown";
    }
}

inline QString clampStateToString(ClampState state) {
    switch (state) {
        case ClampState::Unknown: return "Unknown";
        case ClampState::Open: return "Open";
        case ClampState::Closed: return "Closed";
        case ClampState::Opening: return "Opening";
        case ClampState::Closing: return "Closing";
        case ClampState::Error: return "Error";
        default: return "Unknown";
    }
}

// 注册元类型供信号槽使用
Q_DECLARE_METATYPE(MechanismState)
Q_DECLARE_METATYPE(ClampState)
Q_DECLARE_METATYPE(RobotPosition)
Q_DECLARE_METATYPE(MotorMode)
Q_DECLARE_METATYPE(MotorStatus)
Q_DECLARE_METATYPE(MechanismStatus)

#endif // MECHANISMTYPES_H
