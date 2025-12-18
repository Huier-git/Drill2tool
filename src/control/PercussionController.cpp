#include "control/PercussionController.h"
#include <QDebug>
#include <cmath>

// ============================================================================
// PercussionConfig 实现
// ============================================================================

PercussionConfig PercussionConfig::fromJson(const QJsonObject& json)
{
    PercussionConfig config;
    config.motor = MotorConfig::fromJson(json);
    config.defaultFrequency = json.value("default_frequency").toDouble(5.0);
    config.unlockDAC = json.value("unlock_dac").toDouble(-30.0);
    config.unlockPosition = json.value("unlock_position").toDouble(-100.0);
    config.stableTime = json.value("stable_time").toInt(3000);
    config.positionTolerance = json.value("position_tolerance").toDouble(1.0);
    return config;
}

QJsonObject PercussionConfig::toJson() const
{
    QJsonObject json = motor.toJson();
    json["default_frequency"] = defaultFrequency;
    json["unlock_dac"] = unlockDAC;
    json["unlock_position"] = unlockPosition;
    json["stable_time"] = stableTime;
    json["position_tolerance"] = positionTolerance;
    return json;
}

// ============================================================================
// PercussionController 实现
// ============================================================================

PercussionController::PercussionController(IMotionDriver* driver,
                                           const PercussionConfig& config,
                                           QObject* parent)
    : BaseMechanismController("Percussion", driver, parent)
    , m_config(config)
    , m_isPercussing(false)
    , m_frequency(config.defaultFrequency)
    , m_isLocked(true)  // 默认锁定
    , m_isUnlocking(false)
    , m_lastPosition(0.0)
{
    // 解锁监控定时器
    m_unlockMonitorTimer = new QTimer(this);
    connect(m_unlockMonitorTimer, &QTimer::timeout,
            this, &PercussionController::monitorUnlock);

    // 解锁超时定时器
    m_unlockTimeoutTimer = new QTimer(this);
    m_unlockTimeoutTimer->setSingleShot(true);
    connect(m_unlockTimeoutTimer, &QTimer::timeout,
            this, &PercussionController::onUnlockTimeout);

    qDebug() << QString("[%1] PercussionController created, motor_id=%2")
                .arg(mechanismCodeString())
                .arg(m_config.motor.motorId);
}

PercussionController::~PercussionController()
{
    stop();
}

// ============================================================================
// BaseMechanismController接口实现
// ============================================================================

bool PercussionController::initialize()
{
    setState(MechanismState::Initializing, "Initializing percussion mechanism (Pi)");

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

    // 2. 设置为位置模式（锁定状态）
    if (!driver()->setAxisType(motorId, static_cast<int>(MotorMode::Position))) {
        setError("Failed to set position mode");
        return false;
    }

    reportProgress(66, "Position mode set");

    // 3. 设置运动参数
    driver()->setSpeed(motorId, m_config.motor.defaultSpeed);
    driver()->setAcceleration(motorId, m_config.motor.acceleration);
    driver()->setDeceleration(motorId, m_config.motor.deceleration);

    reportProgress(100, "Initialization complete");

    m_isLocked = true;
    setState(MechanismState::Ready, "Percussion mechanism (Pi) ready - Locked");
    emit initialized();
    return true;
}

bool PercussionController::stop()
{
    if (!checkDriver()) {
        return false;
    }

    int motorId = m_config.motor.motorId;

    // 停止解锁过程
    if (m_isUnlocking) {
        m_unlockMonitorTimer->stop();
        m_unlockTimeoutTimer->stop();
        m_isUnlocking = false;
        driver()->setDAC(motorId, 0);
    }

    // 停止运动
    bool success = driver()->stopAxis(motorId, 2);

    if (success) {
        m_isPercussing = false;
        setState(MechanismState::Holding, "Stopped");
        emit percussionStateChanged(false, m_frequency);
    }

    return success;
}

bool PercussionController::reset()
{
    stop();

    m_isPercussing = false;
    m_isLocked = true;
    m_frequency = m_config.defaultFrequency;

    setState(MechanismState::Ready, "Reset complete");
    emit lockStateChanged(true);
    return true;
}

void PercussionController::updateStatus()
{
    if (!checkDriver()) {
        return;
    }

    int motorId = m_config.motor.motorId;
    double actualVel = driver()->getActualVelocity(motorId);
    bool isPercussingNow = (std::abs(actualVel) > 1.0);

    if (isPercussingNow != m_isPercussing && !m_isUnlocking) {
        m_isPercussing = isPercussingNow;
        emit percussionStateChanged(m_isPercussing, m_frequency);
    }
}

// ============================================================================
// 冲击控制
// ============================================================================

bool PercussionController::startPercussion(double frequency)
{
    if (!checkDriver() || !isReady()) {
        setError("Controller not ready");
        return false;
    }

    // 检查是否已解锁
    if (m_isLocked) {
        setError("Percussion motor is locked. Please unlock first.");
        return false;
    }

    int motorId = m_config.motor.motorId;

    // 使用指定的频率或默认频率
    if (frequency > 0) {
        m_frequency = frequency;
    }

    // 转换频率为速度
    double speed = frequencyToSpeed(m_frequency);

    // 设置为速度模式
    if (!driver()->setAxisType(motorId, static_cast<int>(MotorMode::Velocity))) {
        setError("Failed to set velocity mode");
        return false;
    }

    // 设置速度
    if (!driver()->setSpeed(motorId, speed)) {
        setError("Failed to set percussion speed");
        return false;
    }

    // 开始连续运动
    if (!driver()->moveContinuous(motorId, 1)) {
        setError("Failed to start percussion");
        return false;
    }

    m_isPercussing = true;
    setState(MechanismState::Moving, QString("Percussing at %1 Hz").arg(m_frequency));

    qDebug() << QString("[%1] Percussion started: %2 Hz (speed=%3)")
                .arg(mechanismCodeString()).arg(m_frequency).arg(speed);
    emit percussionStateChanged(true, m_frequency);

    return true;
}

bool PercussionController::stopPercussion()
{
    if (!checkDriver()) {
        return false;
    }

    int motorId = m_config.motor.motorId;

    if (!driver()->stopAxis(motorId, 2)) {
        setError("Failed to stop percussion");
        return false;
    }

    m_isPercussing = false;

    qDebug() << QString("[%1] Percussion stopped").arg(mechanismCodeString());
    emit percussionStateChanged(false, m_frequency);

    setState(MechanismState::Ready, "Stopped");
    return true;
}

bool PercussionController::setFrequency(double frequency)
{
    if (frequency <= 0) {
        return false;
    }

    m_frequency = frequency;

    // 如果正在冲击，更新速度
    if (m_isPercussing && checkDriver()) {
        double speed = frequencyToSpeed(frequency);
        int motorId = m_config.motor.motorId;

        if (driver()->setSpeed(motorId, speed)) {
            qDebug() << QString("[%1] Frequency set to %2 Hz")
                        .arg(mechanismCodeString()).arg(frequency);
            emit percussionStateChanged(true, m_frequency);
            return true;
        }
    }

    return true;
}

// ============================================================================
// 冲击锁定控制
// ============================================================================

bool PercussionController::unlock()
{
    if (!checkDriver() || !isReady()) {
        setError("Controller not ready");
        return false;
    }

    if (!m_isLocked) {
        qDebug() << QString("[%1] Already unlocked").arg(mechanismCodeString());
        return true;
    }

    setState(MechanismState::Initializing, "Unlocking percussion motor");

    int motorId = m_config.motor.motorId;

    // 1. 切换到力矩模式
    if (!driver()->setAxisType(motorId, static_cast<int>(MotorMode::Torque))) {
        setError("Failed to set torque mode");
        return false;
    }

    // 2. 施加反向力矩
    if (!driver()->setDAC(motorId, m_config.unlockDAC)) {
        setError("Failed to set unlock DAC");
        return false;
    }

    reportProgress(30, "Applying unlock torque");

    // 3. 启动监控定时器，等待位置稳定
    m_isUnlocking = true;
    m_lastPosition = driver()->getActualPosition(motorId);
    m_stableStartTime = QTime::currentTime();

    m_unlockMonitorTimer->start(100);  // 100ms检查一次
    m_unlockTimeoutTimer->start(10000);  // 10秒超时

    qDebug() << QString("[%1] Unlock started").arg(mechanismCodeString());

    return true;
}

void PercussionController::monitorUnlock()
{
    if (!m_isUnlocking) {
        m_unlockMonitorTimer->stop();
        return;
    }

    int motorId = m_config.motor.motorId;
    double currentPos = driver()->getActualPosition(motorId);
    double posChange = std::abs(currentPos - m_lastPosition);

    // 检查位置变化
    if (posChange < m_config.positionTolerance) {
        // 位置稳定
        int stableTimeMs = m_stableStartTime.msecsTo(QTime::currentTime());

        if (stableTimeMs >= m_config.stableTime) {
            // 位置已稳定足够长时间，解锁成功
            m_unlockMonitorTimer->stop();
            m_unlockTimeoutTimer->stop();
            m_isUnlocking = false;

            // 切换到位置模式并锁定当前位置
            driver()->setAxisType(motorId, static_cast<int>(MotorMode::Position));
            driver()->setActualPosition(motorId, currentPos);
            driver()->setTargetPosition(motorId, currentPos);

            m_isLocked = false;

            reportProgress(100, "Percussion unlocked");
            setState(MechanismState::Ready, "Percussion motor unlocked");
            emit lockStateChanged(false);
            emit unlockCompleted(true);

            qDebug() << QString("[%1] Unlock completed at position %2")
                        .arg(mechanismCodeString()).arg(currentPos);
        }
    } else {
        // 位置还在变化，重置计时
        m_lastPosition = currentPos;
        m_stableStartTime = QTime::currentTime();
    }
}

void PercussionController::onUnlockTimeout()
{
    if (m_isUnlocking) {
        m_unlockMonitorTimer->stop();
        m_isUnlocking = false;

        int motorId = m_config.motor.motorId;
        driver()->setDAC(motorId, 0);  // 停止施加力矩

        setError("Unlock timeout");
        emit unlockCompleted(false);

        qWarning() << QString("[%1] Unlock timeout").arg(mechanismCodeString());
    }
}

bool PercussionController::lock()
{
    if (!checkDriver()) {
        return false;
    }

    if (m_isLocked) {
        qDebug() << QString("[%1] Already locked").arg(mechanismCodeString());
        return true;
    }

    // 停止冲击
    if (m_isPercussing) {
        stopPercussion();
    }

    int motorId = m_config.motor.motorId;

    // 设置为位置模式并保持当前位置
    driver()->setAxisType(motorId, static_cast<int>(MotorMode::Position));
    double currentPos = driver()->getActualPosition(motorId);
    driver()->setTargetPosition(motorId, currentPos);

    m_isLocked = true;
    emit lockStateChanged(true);

    qDebug() << QString("[%1] Locked").arg(mechanismCodeString());

    return true;
}

// ============================================================================
// 私有函数
// ============================================================================

double PercussionController::frequencyToSpeed(double frequency) const
{
    // 将频率转换为速度（具体转换关系根据实际硬件确定）
    // 这里假设：速度 = 频率 * 转换系数
    return frequency * 1000.0;  // 示例转换
}

// ============================================================================
// 关键位置
// ============================================================================

double PercussionController::getKeyPosition(const QString& key) const
{
    if (m_config.keyPositions.contains(key)) {
        return m_config.keyPositions.value(key);
    }
    return -1.0;
}

bool PercussionController::applyKeyFrequency(const QString& key)
{
    double freq = getKeyPosition(key);
    if (freq < 0) {
        setError(QString("Key position '%1' not found").arg(key));
        return false;
    }

    // A=不冲击 means frequency=0, just stop
    if (freq == 0) {
        return stopPercussion();
    }

    qDebug() << QString("[%1] Applying key frequency %2: %3 Hz")
                .arg(mechanismCodeString()).arg(key).arg(freq);

    return startPercussion(freq);
}

QStringList PercussionController::keyPositionNames() const
{
    return m_config.keyPositions.keys();
}

// ============================================================================
// 配置热更新
// ============================================================================

void PercussionController::updateConfig(const PercussionConfig& config)
{
    qDebug() << QString("[%1] Updating config").arg(mechanismCodeString());
    m_config = config;
    qDebug() << QString("[%1] Config updated").arg(mechanismCodeString());
}
