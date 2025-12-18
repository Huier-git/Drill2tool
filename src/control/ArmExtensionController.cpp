#include "control/ArmExtensionController.h"
#include <QDebug>
#include <cmath>

// ============================================================================
// ArmExtensionConfig 实现
// ============================================================================

ArmExtensionConfig ArmExtensionConfig::fromJson(const QJsonObject& json)
{
    ArmExtensionConfig config;
    config.motor = MotorConfig::fromJson(json);
    config.extendPosition = json.value("extend_position").toDouble(50000.0);
    config.retractPosition = json.value("retract_position").toDouble(0.0);
    config.initDAC = json.value("init_dac").toDouble(-50.0);
    config.stableThreshold = json.value("stable_threshold").toDouble(1.0);
    config.stableCount = json.value("stable_count").toInt(5);
    config.monitorInterval = json.value("monitor_interval").toInt(200);
    return config;
}

QJsonObject ArmExtensionConfig::toJson() const
{
    QJsonObject json = motor.toJson();
    json["extend_position"] = extendPosition;
    json["retract_position"] = retractPosition;
    json["init_dac"] = initDAC;
    json["stable_threshold"] = stableThreshold;
    json["stable_count"] = stableCount;
    json["monitor_interval"] = monitorInterval;
    return json;
}

// ============================================================================
// ArmExtensionController 实现
// ============================================================================

ArmExtensionController::ArmExtensionController(IMotionDriver* driver,
                                               const ArmExtensionConfig& config,
                                               QObject* parent)
    : BaseMechanismController("ArmExtension", driver, parent)
    , m_config(config)
    , m_offset(0.0)
    , m_isMoving(false)
    , m_isInitializing(false)
    , m_lastPosition(0.0)
    , m_stableCount(0)
{
    m_initTimer = new QTimer(this);
    connect(m_initTimer, &QTimer::timeout, this, &ArmExtensionController::monitorInit);

    qDebug() << QString("[%1] ArmExtensionController created, motor_id=%2")
                .arg(mechanismCodeString())
                .arg(m_config.motor.motorId);
}

ArmExtensionController::~ArmExtensionController()
{
    stop();
}

bool ArmExtensionController::initialize()
{
    setState(MechanismState::Initializing, "Initializing arm extension (Me)");

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

    setState(MechanismState::Ready, "Arm extension (Me) ready");
    emit initialized();
    return true;
}

bool ArmExtensionController::stop()
{
    if (!checkDriver()) {
        return false;
    }

    int motorId = m_config.motor.motorId;

    // 停止初始化过程
    if (m_isInitializing) {
        m_initTimer->stop();
        m_isInitializing = false;
        driver()->setDAC(motorId, 0);
    }

    // 停止运动
    bool success = driver()->stopAxis(motorId, 2);

    if (success) {
        m_isMoving = false;
        setState(MechanismState::Holding, "Stopped");
    }

    return success;
}

bool ArmExtensionController::reset()
{
    stop();
    m_offset = 0.0;
    m_isMoving = false;
    setState(MechanismState::Ready, "Reset complete");
    return true;
}

void ArmExtensionController::updateStatus()
{
    if (!checkDriver()) {
        return;
    }

    double pos = currentPosition();
    emit positionChanged(pos);
}

// ============================================================================
// 伸缩控制
// ============================================================================

bool ArmExtensionController::setPosition(double position)
{
    if (!checkDriver() || !isReady()) {
        setError("Controller not ready");
        return false;
    }

    int motorId = m_config.motor.motorId;

    // 应用偏移
    double targetPos = position - m_offset;

    if (!driver()->moveAbsolute(motorId, targetPos)) {
        setError("Failed to start movement");
        return false;
    }

    m_isMoving = true;
    setState(MechanismState::Moving, QString("Moving to %1").arg(position));

    qDebug() << QString("[%1] Moving to position %2")
                .arg(mechanismCodeString()).arg(position);

    return true;
}

bool ArmExtensionController::extend()
{
    qDebug() << QString("[%1] Extending").arg(mechanismCodeString());
    return setPosition(m_config.extendPosition);
}

bool ArmExtensionController::retract()
{
    qDebug() << QString("[%1] Retracting").arg(mechanismCodeString());
    return setPosition(m_config.retractPosition);
}

double ArmExtensionController::currentPosition() const
{
    if (!checkDriver()) {
        return 0.0;
    }

    int motorId = m_config.motor.motorId;
    double position = driver()->getActualPosition(motorId);
    return position + m_offset;
}

// ============================================================================
// 初始化
// ============================================================================

bool ArmExtensionController::initializePosition()
{
    if (!checkDriver() || !isReady()) {
        setError("Controller not ready");
        return false;
    }

    setState(MechanismState::Initializing, "Finding home position");

    int motorId = m_config.motor.motorId;

    // 切换到力矩模式
    driver()->setAxisType(motorId, static_cast<int>(MotorMode::Torque));

    // 施加反向力矩（回收方向）
    driver()->setDAC(motorId, m_config.initDAC);

    // 启动监控
    m_isInitializing = true;
    m_lastPosition = driver()->getActualPosition(motorId);
    m_stableCount = 0;

    m_initTimer->start(m_config.monitorInterval);

    qDebug() << QString("[%1] Position initialization started")
                .arg(mechanismCodeString());

    return true;
}

void ArmExtensionController::monitorInit()
{
    if (!m_isInitializing) {
        m_initTimer->stop();
        return;
    }

    int motorId = m_config.motor.motorId;
    double currentPos = driver()->getActualPosition(motorId);
    double posChange = std::abs(currentPos - m_lastPosition);

    if (posChange < m_config.stableThreshold) {
        m_stableCount++;

        if (m_stableCount >= m_config.stableCount) {
            // 初始化完成
            m_initTimer->stop();
            m_isInitializing = false;

            // 切换到位置模式并设为零点
            driver()->setDAC(motorId, 0);
            driver()->setAxisType(motorId, static_cast<int>(MotorMode::Position));
            driver()->setActualPosition(motorId, 0.0);
            driver()->setTargetPosition(motorId, 0.0);

            m_offset = 0.0;

            setState(MechanismState::Ready, "Position initialized");
            emit targetReached();

            qDebug() << QString("[%1] Position initialization completed")
                        .arg(mechanismCodeString());
        }
    } else {
        m_stableCount = 0;
        m_lastPosition = currentPos;
    }
}

bool ArmExtensionController::resetZero()
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
// 关键位置
// ============================================================================

double ArmExtensionController::getKeyPosition(const QString& key) const
{
    if (m_config.keyPositions.contains(key)) {
        return m_config.keyPositions.value(key);
    }
    return -1.0;
}

bool ArmExtensionController::moveToKeyPosition(const QString& key)
{
    double position = getKeyPosition(key);
    if (position < 0) {
        setError(QString("Key position '%1' not found").arg(key));
        return false;
    }

    qDebug() << QString("[%1] Moving to key position %2 (%3)")
                .arg(mechanismCodeString()).arg(key).arg(position);

    return setPosition(position);
}

QStringList ArmExtensionController::keyPositionNames() const
{
    return m_config.keyPositions.keys();
}

// ============================================================================
// 配置热更新
// ============================================================================

void ArmExtensionController::updateConfig(const ArmExtensionConfig& config)
{
    qDebug() << QString("[%1] Updating config").arg(mechanismCodeString());
    m_config = config;
    qDebug() << QString("[%1] Config updated").arg(mechanismCodeString());
}
