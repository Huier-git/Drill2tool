#include "control/ArmRotationController.h"
#include <QDebug>
#include <cmath>

// ============================================================================
// ArmRotationConfig 实现
// ============================================================================

ArmRotationConfig ArmRotationConfig::fromJson(const QJsonObject& json)
{
    ArmRotationConfig config;
    config.motor = MotorConfig::fromJson(json);
    config.drillPositionAngle = json.value("drill_position_angle").toDouble(0.0);
    config.storagePositionAngle = json.value("storage_position_angle").toDouble(180.0);
    config.pulsesPerDegree = json.value("pulses_per_degree").toDouble(1000.0);
    config.positionTolerance = json.value("position_tolerance").toDouble(0.5);
    return config;
}

QJsonObject ArmRotationConfig::toJson() const
{
    QJsonObject json = motor.toJson();
    json["drill_position_angle"] = drillPositionAngle;
    json["storage_position_angle"] = storagePositionAngle;
    json["pulses_per_degree"] = pulsesPerDegree;
    json["position_tolerance"] = positionTolerance;
    return json;
}

// ============================================================================
// ArmRotationController 实现
// ============================================================================

ArmRotationController::ArmRotationController(IMotionDriver* driver,
                                             const ArmRotationConfig& config,
                                             QObject* parent)
    : BaseMechanismController("ArmRotation", driver, parent)
    , m_config(config)
    , m_offset(0.0)
    , m_isRotating(false)
    , m_currentPosition(ArmPosition::Unknown)
{
    qDebug() << QString("[%1] ArmRotationController created, motor_id=%2")
                .arg(mechanismCodeString())
                .arg(m_config.motor.motorId);
}

ArmRotationController::~ArmRotationController()
{
    stop();
}

bool ArmRotationController::initialize()
{
    setState(MechanismState::Initializing, "Initializing arm rotation (Mr)");

    if (!checkDriver()) {
        setError("Driver not available");
        return false;
    }

    int motorId = m_config.motor.motorId;

    // 1. 使能轴
    if (!driver()->setAxisEnable(motorId, true)) {
        setError(QString("Failed to enable axis %1").arg(motorId));
        return false;
    }

    reportProgress(50, "Axis enabled");

    // 2. 设置为位置模式
    if (!driver()->setAxisType(motorId, static_cast<int>(MotorMode::Position))) {
        setError("Failed to set position mode");
        return false;
    }

    // 3. 设置运动参数
    driver()->setSpeed(motorId, m_config.motor.defaultSpeed);
    driver()->setAcceleration(motorId, m_config.motor.acceleration);
    driver()->setDeceleration(motorId, m_config.motor.deceleration);

    reportProgress(100, "Initialization complete");

    // 确定当前位置
    m_currentPosition = determinePosition(currentAngle());

    setState(MechanismState::Ready, "Arm rotation (Mr) ready");
    emit initialized();
    return true;
}

bool ArmRotationController::stop()
{
    if (!checkDriver()) {
        return false;
    }

    int motorId = m_config.motor.motorId;

    // 停止运动
    bool success = driver()->stopAxis(motorId, 2);

    if (success) {
        m_isRotating = false;
        setState(MechanismState::Holding, "Stopped");
    }

    return success;
}

bool ArmRotationController::reset()
{
    stop();
    m_offset = 0.0;
    m_isRotating = false;
    m_currentPosition = ArmPosition::Unknown;
    setState(MechanismState::Ready, "Reset complete");
    return true;
}

void ArmRotationController::updateStatus()
{
    if (!checkDriver()) {
        return;
    }

    double angle = currentAngle();
    emit angleChanged(angle);

    // 检测位置到达
    ArmPosition newPos = determinePosition(angle);
    if (newPos != m_currentPosition && newPos != ArmPosition::Unknown) {
        m_currentPosition = newPos;
        emit positionReached(newPos);
    }
}

// ============================================================================
// 回转控制
// ============================================================================

bool ArmRotationController::setAngle(double angle)
{
    if (!checkDriver() || !isReady()) {
        setError("Controller not ready");
        return false;
    }

    int motorId = m_config.motor.motorId;

    // 转换角度为脉冲位置
    double targetPulses = angleToPulses(angle - m_offset);

    if (!driver()->moveAbsolute(motorId, targetPulses)) {
        setError("Failed to start rotation movement");
        return false;
    }

    m_isRotating = true;
    setState(MechanismState::Moving, QString("Rotating to %1°").arg(angle));

    qDebug() << QString("[%1] Rotating to %2°")
                .arg(mechanismCodeString()).arg(angle);

    return true;
}

bool ArmRotationController::rotateToPosition(ArmPosition position)
{
    double targetAngle = 0.0;
    QString posName;

    switch (position) {
        case ArmPosition::Drill:
            targetAngle = m_config.drillPositionAngle;
            posName = "Drill";
            break;
        case ArmPosition::Storage:
            targetAngle = m_config.storagePositionAngle;
            posName = "Storage";
            break;
        default:
            setError("Invalid arm position");
            return false;
    }

    qDebug() << QString("[%1] Rotating to %2 position (%3°)")
                .arg(mechanismCodeString())
                .arg(posName)
                .arg(targetAngle);

    return setAngle(targetAngle);
}

bool ArmRotationController::rotateToDrill()
{
    return rotateToPosition(ArmPosition::Drill);
}

bool ArmRotationController::rotateToStorage()
{
    return rotateToPosition(ArmPosition::Storage);
}

double ArmRotationController::currentAngle() const
{
    if (!checkDriver()) {
        return 0.0;
    }

    int motorId = m_config.motor.motorId;
    double pulses = driver()->getActualPosition(motorId);
    return pulsesToAngle(pulses) + m_offset;
}

ArmPosition ArmRotationController::currentPosition() const
{
    return m_currentPosition;
}

// ============================================================================
// 零点管理
// ============================================================================

bool ArmRotationController::resetZero()
{
    if (!checkDriver()) {
        return false;
    }

    int motorId = m_config.motor.motorId;

    driver()->setActualPosition(motorId, 0.0);
    driver()->setTargetPosition(motorId, 0.0);

    m_offset = 0.0;

    qDebug() << QString("[%1] Zero reset").arg(mechanismCodeString());
    return true;
}

// ============================================================================
// 辅助函数
// ============================================================================

double ArmRotationController::angleToPulses(double angle) const
{
    return angle * m_config.pulsesPerDegree;
}

double ArmRotationController::pulsesToAngle(double pulses) const
{
    return pulses / m_config.pulsesPerDegree;
}

ArmPosition ArmRotationController::determinePosition(double angle) const
{
    double tolerance = m_config.positionTolerance;

    if (std::abs(angle - m_config.drillPositionAngle) <= tolerance) {
        return ArmPosition::Drill;
    }

    if (std::abs(angle - m_config.storagePositionAngle) <= tolerance) {
        return ArmPosition::Storage;
    }

    return ArmPosition::Unknown;
}

// ============================================================================
// 关键位置
// ============================================================================

double ArmRotationController::getKeyPosition(const QString& key) const
{
    if (m_config.keyPositions.contains(key)) {
        return m_config.keyPositions.value(key);
    }
    return -1.0;
}

bool ArmRotationController::moveToKeyPosition(const QString& key)
{
    double angle = getKeyPosition(key);
    if (angle < 0) {
        setError(QString("Key position '%1' not found").arg(key));
        return false;
    }

    qDebug() << QString("[%1] Moving to key position %2 (%3°)")
                .arg(mechanismCodeString()).arg(key).arg(angle);

    return setAngle(angle);
}

QStringList ArmRotationController::keyPositionNames() const
{
    return m_config.keyPositions.keys();
}

// ============================================================================
// 配置热更新
// ============================================================================

void ArmRotationController::updateConfig(const ArmRotationConfig& config)
{
    qDebug() << QString("[%1] Updating config").arg(mechanismCodeString());
    m_config = config;
    qDebug() << QString("[%1] Config updated").arg(mechanismCodeString());
}
