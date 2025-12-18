#include "control/FeedController.h"
#include <QDebug>
#include <cmath>

FeedController::FeedController(IMotionDriver* driver,
                               const PenetrationConfig& config,
                               QObject* parent)
    : BaseMechanismController("Feed", driver, parent)
    , m_config(config)
    , m_targetDepth(0.0)
    , m_currentSpeed(config.motor.defaultSpeed)
    , m_zeroOffsetMm(0.0)
    , m_isMoving(false)
    , m_monitorTimer(nullptr)
{
    // 创建运动监控定时器
    m_monitorTimer = new QTimer(this);
    connect(m_monitorTimer, &QTimer::timeout, this, [this]() {
        if (m_isMoving) {
            double currentPos = currentDepth();
            double error = std::abs(currentPos - m_targetDepth);

            // 如果误差小于0.5mm，认为到达目标
            if (error < 0.5) {
                m_isMoving = false;
                m_monitorTimer->stop();
                setState(MechanismState::Holding,
                        QString("Reached target depth: %1 mm").arg(currentPos));
                emit targetReached();
            }
        }
    });

    qDebug() << QString("[%1] FeedController created, motor_id=%2")
                .arg(mechanismCodeString())
                .arg(m_config.motor.motorId);
}

FeedController::~FeedController()
{
    if (m_monitorTimer) {
        m_monitorTimer->stop();
    }
}

// ============================================================================
// BaseMechanismController接口实现
// ============================================================================

bool FeedController::initialize()
{
    setState(MechanismState::Initializing, "Initializing feed mechanism (Fz)");

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

    // 2. 设置为位置模式
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

    setState(MechanismState::Ready, "Feed mechanism (Fz) ready");
    emit initialized();
    return true;
}

bool FeedController::stop()
{
    if (!checkDriver()) {
        return false;
    }

    int motorId = m_config.motor.motorId;
    bool success = driver()->stopAxis(motorId, 2);

    if (success) {
        m_isMoving = false;
        if (m_monitorTimer) {
            m_monitorTimer->stop();
        }
        setState(MechanismState::Holding, "Stopped");
    }

    return success;
}

bool FeedController::reset()
{
    if (!checkDriver()) {
        return false;
    }

    // 停止运动
    stop();

    // 重置零点偏移
    m_zeroOffsetMm = 0.0;
    m_targetDepth = 0.0;

    setState(MechanismState::Ready, "Reset complete");
    return true;
}

void FeedController::updateStatus()
{
    if (!checkDriver()) {
        return;
    }

    // 获取当前深度并发送信号
    double depth = currentDepth();
    emit depthChanged(depth);

    // 检查限位
    if (depth >= m_config.depthLimits.maxDepthMm) {
        emit limitReached(true);
    } else if (depth <= m_config.depthLimits.minDepthMm) {
        emit limitReached(false);
    }
}

// ============================================================================
// 深度控制
// ============================================================================

bool FeedController::setTargetDepth(double depthMm, double speed)
{
    if (!checkDriver() || !isReady()) {
        setError("Controller not ready");
        return false;
    }

    // 安全检查
    if (!checkSafetyLimits(depthMm)) {
        setError(QString("Target depth %1mm exceeds safety limits [%2, %3]mm")
                .arg(depthMm)
                .arg(m_config.depthLimits.minDepthMm)
                .arg(m_config.depthLimits.maxDepthMm));
        return false;
    }

    m_targetDepth = depthMm;

    // 设置速度
    if (speed > 0) {
        setSpeed(speed);
    }

    return moveToDepth(depthMm, m_currentSpeed);
}

bool FeedController::stopFeed()
{
    return stop();
}

bool FeedController::gotoSafePosition()
{
    qDebug() << QString("[%1] Moving to safe position: %2mm")
                .arg(mechanismCodeString())
                .arg(m_config.depthLimits.safeDepthMm);

    return setTargetDepth(m_config.depthLimits.safeDepthMm);
}

bool FeedController::moveToDepth(double depthMm, double speed)
{
    if (!checkDriver()) {
        return false;
    }

    int motorId = m_config.motor.motorId;

    // 转换为脉冲
    double targetPulses = mmToPulses(depthMm);

    // 设置速度
    if (speed > 0) {
        driver()->setSpeed(motorId, speed);
    }

    // 发送绝对运动指令
    if (!driver()->moveAbsolute(motorId, targetPulses)) {
        setError("Failed to start movement");
        return false;
    }

    m_isMoving = true;
    setState(MechanismState::Moving, QString("Moving to depth %1mm").arg(depthMm));

    // 启动监控定时器
    if (!m_monitorTimer->isActive()) {
        m_monitorTimer->start(100);
    }

    return true;
}

bool FeedController::moveUp(double distanceMm)
{
    double newDepth = currentDepth() + distanceMm;
    return setTargetDepth(newDepth);
}

bool FeedController::moveDown(double distanceMm)
{
    double newDepth = currentDepth() - distanceMm;
    return setTargetDepth(newDepth);
}

// ============================================================================
// 深度查询
// ============================================================================

double FeedController::currentDepth() const
{
    if (!checkDriver()) {
        return 0.0;
    }

    int motorId = m_config.motor.motorId;
    double pulses = driver()->getActualPosition(motorId);

    return pulsesToMm(pulses);
}

double FeedController::currentPulse() const
{
    if (!checkDriver()) {
        return 0.0;
    }

    int motorId = m_config.motor.motorId;
    return driver()->getActualPosition(motorId);
}

// ============================================================================
// 限位管理
// ============================================================================

bool FeedController::setDepthLimits(double minMm, double maxMm)
{
    if (maxMm <= minMm) {
        qWarning() << QString("[%1] Invalid depth limits: max(%2) <= min(%3)")
                      .arg(mechanismCodeString()).arg(maxMm).arg(minMm);
        return false;
    }

    m_config.depthLimits.minDepthMm = minMm;
    m_config.depthLimits.maxDepthMm = maxMm;

    qDebug() << QString("[%1] Depth limits set: [%2, %3]mm")
                .arg(mechanismCodeString()).arg(minMm).arg(maxMm);

    return true;
}

void FeedController::setZeroOffset(double offsetMm)
{
    m_zeroOffsetMm = offsetMm;

    qDebug() << QString("[%1] Zero offset set to %2mm")
                .arg(mechanismCodeString()).arg(offsetMm);
}

// ============================================================================
// 速度控制
// ============================================================================

bool FeedController::setSpeed(double speed)
{
    if (!checkDriver()) {
        return false;
    }

    // 限制速度范围
    speed = qBound(m_config.motor.minSpeed, speed, m_config.motor.maxSpeed);

    int motorId = m_config.motor.motorId;
    if (driver()->setSpeed(motorId, speed)) {
        m_currentSpeed = speed;
        qDebug() << QString("[%1] Speed set to %2")
                    .arg(mechanismCodeString()).arg(speed);
        return true;
    }

    return false;
}

// ============================================================================
// 私有辅助函数
// ============================================================================

double FeedController::mmToPulses(double mm) const
{
    // 深度转换：顶部为最大深度，底部为最小深度
    double adjustedMm = mm - m_zeroOffsetMm;
    double pulses = (m_config.depthLimits.maxDepthMm - adjustedMm) * m_config.pulsesPerMm;
    return pulses;
}

double FeedController::pulsesToMm(double pulses) const
{
    // 反向转换
    double mm = m_config.depthLimits.maxDepthMm - (pulses / m_config.pulsesPerMm);
    return mm + m_zeroOffsetMm;
}

bool FeedController::checkSafetyLimits(double depthMm) const
{
    return (depthMm >= m_config.depthLimits.minDepthMm &&
            depthMm <= m_config.depthLimits.maxDepthMm);
}

// ============================================================================
// 关键位置
// ============================================================================

double FeedController::getKeyPosition(const QString& key) const
{
    if (m_config.keyPositions.contains(key)) {
        return m_config.keyPositions.value(key);
    }
    return -1.0;
}

bool FeedController::moveToKeyPosition(const QString& key)
{
    double pulses = getKeyPosition(key);
    if (pulses < 0) {
        setError(QString("Key position '%1' not found").arg(key));
        return false;
    }

    if (!checkDriver()) {
        return false;
    }

    int motorId = m_config.motor.motorId;

    // 直接移动到脉冲位置
    if (!driver()->moveAbsolute(motorId, pulses)) {
        setError(QString("Failed to move to key position '%1'").arg(key));
        return false;
    }

    m_isMoving = true;
    setState(MechanismState::Moving, QString("Moving to key position %1").arg(key));

    // 启动监控定时器
    if (!m_monitorTimer->isActive()) {
        m_monitorTimer->start(100);
    }

    qDebug() << QString("[%1] Moving to key position %2 (%3 pulses)")
                .arg(mechanismCodeString()).arg(key).arg(pulses);

    return true;
}

QStringList FeedController::keyPositionNames() const
{
    return m_config.keyPositions.keys();
}

// ============================================================================
// 配置热更新
// ============================================================================

void FeedController::updateConfig(const PenetrationConfig& config)
{
    qDebug() << QString("[%1] Updating config").arg(mechanismCodeString());

    // 更新配置（保留运行时状态）
    m_config = config;

    // 更新速度（如果没有运动中）
    if (!m_isMoving) {
        m_currentSpeed = config.motor.defaultSpeed;
    }

    qDebug() << QString("[%1] Config updated, keyPositions=%2")
                .arg(mechanismCodeString())
                .arg(m_config.keyPositions.count());
}
