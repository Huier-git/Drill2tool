#include "control/StorageController.h"
#include <QDebug>
#include <cmath>

StorageController::StorageController(IMotionDriver* driver,
                                     const StorageConfig& config,
                                     QObject* parent)
    : BaseMechanismController("Storage", driver, parent)
    , m_config(config)
    , m_currentPosition(0)
    , m_angleOffset(0.0)
{
    qDebug() << QString("[%1] StorageController created, motor_id=%2, positions=%3")
                .arg(mechanismCodeString())
                .arg(m_config.motor.motorId)
                .arg(m_config.positions);
}

StorageController::~StorageController()
{
    stop();
}

bool StorageController::initialize()
{
    setState(MechanismState::Initializing, "Initializing storage mechanism (Sr)");

    if (!checkDriver()) {
        setError("Driver not available");
        return false;
    }
    
    int motorId = m_config.motor.motorId;
    
    if (!driver()->setAxisEnable(motorId, true)) {
        setError("Failed to enable motor");
        return false;
    }
    
    driver()->setAxisType(motorId, static_cast<int>(MotorMode::Position));
    driver()->setSpeed(motorId, m_config.motor.defaultSpeed);
    driver()->setAcceleration(motorId, m_config.motor.acceleration);

    setState(MechanismState::Ready, "Storage mechanism (Sr) ready");
    emit initialized();
    return true;
}

bool StorageController::stop()
{
    if (!checkDriver()) {
        return false;
    }
    
    bool success = driver()->stopAxis(m_config.motor.motorId, 2);
    
    if (success) {
        setState(MechanismState::Holding, "Stopped");
    }
    
    return success;
}

bool StorageController::reset()
{
    stop();
    m_currentPosition = 0;
    m_angleOffset = 0.0;
    setState(MechanismState::Ready, "Reset complete");
    return true;
}

void StorageController::updateStatus()
{
    // 状态更新
}

bool StorageController::moveForward()
{
    int nextPos = (m_currentPosition + 1) % m_config.positions;
    return moveToPosition(nextPos);
}

bool StorageController::moveBackward()
{
    int nextPos = (m_currentPosition - 1 + m_config.positions) % m_config.positions;
    return moveToPosition(nextPos);
}

bool StorageController::moveToPosition(int position)
{
    if (!checkDriver() || !isReady()) {
        setError("Controller not ready");
        return false;
    }
    
    if (position < 0 || position >= m_config.positions) {
        setError(QString("Invalid position: %1 (valid: 0-%2)")
                .arg(position).arg(m_config.positions - 1));
        return false;
    }
    
    double targetAngle = positionToAngle(position);
    int motorId = m_config.motor.motorId;
    
    // 转换角度为电机位置（假设1度=1000脉冲）
    double targetPulses = (targetAngle - m_angleOffset) * 1000.0;
    
    if (!driver()->moveAbsolute(motorId, targetPulses)) {
        setError("Failed to start movement");
        return false;
    }
    
    m_currentPosition = position;
    setState(MechanismState::Moving, QString("Moving to position %1").arg(position));
    emit positionChanged(position);

    qDebug() << QString("[%1] Moving to position %2 (angle %3°)")
                .arg(mechanismCodeString()).arg(position).arg(targetAngle);
    
    return true;
}

bool StorageController::resetZero()
{
    if (!checkDriver()) {
        return false;
    }
    
    int motorId = m_config.motor.motorId;
    driver()->setActualPosition(motorId, 0.0);
    driver()->setTargetPosition(motorId, 0.0);
    
    m_angleOffset = 0.0;
    m_currentPosition = 0;

    qDebug() << QString("[%1] Zero point reset").arg(mechanismCodeString());
    return true;
}

double StorageController::positionToAngle(int position) const
{
    return position * m_config.anglePerPosition;
}

// ============================================================================
// 关键位置
// ============================================================================

double StorageController::getKeyPosition(const QString& key) const
{
    if (m_config.keyPositions.contains(key)) {
        return m_config.keyPositions.value(key);
    }
    return -1.0;
}

bool StorageController::moveToKeyPosition(const QString& key)
{
    double pulses = getKeyPosition(key);
    if (pulses < 0) {
        setError(QString("Key position '%1' not found").arg(key));
        return false;
    }

    if (!checkDriver() || !isReady()) {
        setError("Controller not ready");
        return false;
    }

    int motorId = m_config.motor.motorId;

    // 直接移动到脉冲位置
    if (!driver()->moveAbsolute(motorId, pulses)) {
        setError(QString("Failed to move to key position '%1'").arg(key));
        return false;
    }

    setState(MechanismState::Moving, QString("Moving to key position %1").arg(key));

    qDebug() << QString("[%1] Moving to key position %2 (%3 pulses)")
                .arg(mechanismCodeString()).arg(key).arg(pulses);

    return true;
}

QStringList StorageController::keyPositionNames() const
{
    return m_config.keyPositions.keys();
}

// ============================================================================
// 配置热更新
// ============================================================================

void StorageController::updateConfig(const StorageConfig& config)
{
    qDebug() << QString("[%1] Updating config").arg(mechanismCodeString());
    m_config = config;
    qDebug() << QString("[%1] Config updated").arg(mechanismCodeString());
}
