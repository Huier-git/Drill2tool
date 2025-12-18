#include "control/RotationController.h"
#include <QDebug>
#include <cmath>

// ============================================================================
// RotationConfig 实现
// ============================================================================

RotationConfig RotationConfig::fromJson(const QJsonObject& json)
{
    RotationConfig config;
    config.motor = MotorConfig::fromJson(json);
    config.defaultSpeed = json.value("default_speed").toDouble(60.0);
    config.maxTorque = json.value("max_torque").toDouble(100.0);
    config.minTorque = json.value("min_torque").toDouble(-100.0);
    return config;
}

QJsonObject RotationConfig::toJson() const
{
    QJsonObject json = motor.toJson();
    json["default_speed"] = defaultSpeed;
    json["max_torque"] = maxTorque;
    json["min_torque"] = minTorque;
    return json;
}

// ============================================================================
// RotationController 实现
// ============================================================================

RotationController::RotationController(IMotionDriver* driver,
                                       const RotationConfig& config,
                                       QObject* parent)
    : BaseMechanismController("Rotation", driver, parent)
    , m_config(config)
    , m_isRotating(false)
    , m_speed(config.defaultSpeed)
    , m_isTorqueMode(false)
{
    qDebug() << QString("[%1] RotationController created, motor_id=%2")
                .arg(mechanismCodeString())
                .arg(m_config.motor.motorId);
}

RotationController::~RotationController()
{
    stop();
}

// ============================================================================
// BaseMechanismController接口实现
// ============================================================================

bool RotationController::initialize()
{
    setState(MechanismState::Initializing, "Initializing rotation mechanism (Pr)");

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

    reportProgress(33, "Axis enabled");

    // 2. 设置为速度模式
    if (!driver()->setAxisType(motorId, static_cast<int>(MotorMode::Velocity))) {
        setError("Failed to set velocity mode");
        return false;
    }

    reportProgress(66, "Velocity mode set");

    // 3. 设置运动参数
    driver()->setSpeed(motorId, m_config.motor.defaultSpeed);
    driver()->setAcceleration(motorId, m_config.motor.acceleration);
    driver()->setDeceleration(motorId, m_config.motor.deceleration);

    reportProgress(100, "Initialization complete");

    m_isTorqueMode = false;
    setState(MechanismState::Ready, "Rotation mechanism (Pr) ready");
    emit initialized();
    return true;
}

bool RotationController::stop()
{
    if (!checkDriver()) {
        return false;
    }

    int motorId = m_config.motor.motorId;

    // 停止力矩输出
    if (m_isTorqueMode) {
        driver()->setDAC(motorId, 0);
    }

    // 停止运动
    bool success = driver()->stopAxis(motorId, 2);

    if (success) {
        m_isRotating = false;
        setState(MechanismState::Holding, "Stopped");
        emit rotationStateChanged(false, m_speed);
    }

    return success;
}

bool RotationController::reset()
{
    stop();

    m_isRotating = false;
    m_isTorqueMode = false;
    m_speed = m_config.defaultSpeed;

    setState(MechanismState::Ready, "Reset complete");
    return true;
}

void RotationController::updateStatus()
{
    if (!checkDriver()) {
        return;
    }

    int motorId = m_config.motor.motorId;
    double actualVel = driver()->getActualVelocity(motorId);
    bool isRotatingNow = (std::abs(actualVel) > 1.0);

    if (isRotatingNow != m_isRotating) {
        m_isRotating = isRotatingNow;
        emit rotationStateChanged(m_isRotating, m_speed);
    }
}

// ============================================================================
// 旋转控制
// ============================================================================

bool RotationController::startRotation(double rpm)
{
    if (!checkDriver() || !isReady()) {
        setError("Controller not ready");
        return false;
    }

    int motorId = m_config.motor.motorId;

    // 使用指定的转速或默认转速
    if (rpm != -1) {
        m_speed = rpm;
    }

    // 确保是速度模式
    if (m_isTorqueMode) {
        if (!driver()->setAxisType(motorId, static_cast<int>(MotorMode::Velocity))) {
            setError("Failed to set velocity mode");
            return false;
        }
        m_isTorqueMode = false;
    }

    // 设置速度
    double absSpeed = std::abs(m_speed);
    if (!driver()->setSpeed(motorId, absSpeed)) {
        setError("Failed to set rotation speed");
        return false;
    }

    // 开始连续运动（方向由速度符号决定）
    int direction = (m_speed >= 0) ? 1 : -1;
    if (!driver()->moveContinuous(motorId, direction)) {
        setError("Failed to start rotation");
        return false;
    }

    m_isRotating = true;
    setState(MechanismState::Moving, QString("Rotating at %1 rpm").arg(m_speed));

    qDebug() << QString("[%1] Rotation started: %2 rpm")
                .arg(mechanismCodeString()).arg(m_speed);
    emit rotationStateChanged(true, m_speed);

    return true;
}

bool RotationController::stopRotation()
{
    if (!checkDriver()) {
        return false;
    }

    int motorId = m_config.motor.motorId;

    if (!driver()->stopAxis(motorId, 2)) {
        setError("Failed to stop rotation");
        return false;
    }

    m_isRotating = false;

    qDebug() << QString("[%1] Rotation stopped").arg(mechanismCodeString());
    emit rotationStateChanged(false, m_speed);

    setState(MechanismState::Ready, "Stopped");
    return true;
}

bool RotationController::setSpeed(double rpm)
{
    if (!checkDriver()) {
        return false;
    }

    // 限制速度范围
    rpm = qBound(-m_config.motor.maxSpeed, rpm, m_config.motor.maxSpeed);

    int motorId = m_config.motor.motorId;

    if (driver()->setSpeed(motorId, std::abs(rpm))) {
        m_speed = rpm;
        qDebug() << QString("[%1] Speed set to %2 rpm")
                    .arg(mechanismCodeString()).arg(rpm);

        emit speedChanged(m_speed);

        // 如果正在旋转，可能需要更新方向
        if (m_isRotating) {
            int direction = (rpm >= 0) ? 1 : -1;
            driver()->moveContinuous(motorId, direction);
            emit rotationStateChanged(true, m_speed);
        }

        return true;
    }

    return false;
}

double RotationController::actualSpeed() const
{
    if (!checkDriver()) {
        return 0.0;
    }

    int motorId = m_config.motor.motorId;
    return driver()->getActualVelocity(motorId);
}

// ============================================================================
// 力矩模式
// ============================================================================

bool RotationController::setTorque(double dac)
{
    if (!checkDriver()) {
        return false;
    }

    int motorId = m_config.motor.motorId;

    // 限制力矩范围
    dac = qBound(m_config.minTorque, dac, m_config.maxTorque);

    // 切换到力矩模式
    if (!m_isTorqueMode) {
        if (!driver()->setAxisType(motorId, static_cast<int>(MotorMode::Torque))) {
            setError("Failed to set torque mode");
            return false;
        }
        m_isTorqueMode = true;
    }

    // 设置力矩
    if (!driver()->setDAC(motorId, dac)) {
        setError("Failed to set torque");
        return false;
    }

    m_isRotating = (std::abs(dac) > 1.0);
    setState(MechanismState::Moving, QString("Torque mode: DAC=%1").arg(dac));

    qDebug() << QString("[%1] Torque set to %2")
                .arg(mechanismCodeString()).arg(dac);

    return true;
}

bool RotationController::stopTorque()
{
    if (!checkDriver()) {
        return false;
    }

    int motorId = m_config.motor.motorId;

    // 停止力矩输出
    driver()->setDAC(motorId, 0);

    // 切换回速度模式
    driver()->setAxisType(motorId, static_cast<int>(MotorMode::Velocity));
    m_isTorqueMode = false;
    m_isRotating = false;

    setState(MechanismState::Ready, "Torque stopped");
    emit rotationStateChanged(false, m_speed);

    return true;
}

// ============================================================================
// 关键位置
// ============================================================================

double RotationController::getKeyPosition(const QString& key) const
{
    if (m_config.keyPositions.contains(key)) {
        return m_config.keyPositions.value(key);
    }
    return -1.0;
}

bool RotationController::applyKeySpeed(const QString& key)
{
    double speed = getKeyPosition(key);
    if (speed < 0) {
        setError(QString("Key position '%1' not found").arg(key));
        return false;
    }

    // A=不旋转 means speed=0, just stop
    if (speed == 0) {
        return stopRotation();
    }

    qDebug() << QString("[%1] Applying key speed %2: %3 rpm")
                .arg(mechanismCodeString()).arg(key).arg(speed);

    return startRotation(speed);
}

QStringList RotationController::keyPositionNames() const
{
    return m_config.keyPositions.keys();
}

// ============================================================================
// 配置热更新
// ============================================================================

void RotationController::updateConfig(const RotationConfig& config)
{
    qDebug() << QString("[%1] Updating config").arg(mechanismCodeString());
    m_config = config;
    qDebug() << QString("[%1] Config updated").arg(mechanismCodeString());
}
