#include "control/ArmGripController.h"
#include <QDebug>
#include <cmath>

// ============================================================================
// ArmGripConfig 实现
// ============================================================================

ArmGripConfig ArmGripConfig::fromJson(const QJsonObject& json)
{
    ArmGripConfig config;
    config.motor = MotorConfig::fromJson(json);
    config.openDAC = json.value("open_dac").toDouble(-100.0);
    config.closeDAC = json.value("close_dac").toDouble(100.0);
    config.initDAC = json.value("init_dac").toDouble(10.0);
    config.maxDAC = json.value("max_dac").toDouble(80.0);
    config.dacIncrement = json.value("dac_increment").toDouble(10.0);
    config.stableThreshold = json.value("stable_threshold").toDouble(1.0);
    config.stableCount = json.value("stable_count").toInt(5);
    config.monitorInterval = json.value("monitor_interval").toInt(200);
    return config;
}

QJsonObject ArmGripConfig::toJson() const
{
    QJsonObject json = motor.toJson();
    json["open_dac"] = openDAC;
    json["close_dac"] = closeDAC;
    json["init_dac"] = initDAC;
    json["max_dac"] = maxDAC;
    json["dac_increment"] = dacIncrement;
    json["stable_threshold"] = stableThreshold;
    json["stable_count"] = stableCount;
    json["monitor_interval"] = monitorInterval;
    return json;
}

// ============================================================================
// ArmGripController 实现
// ============================================================================

ArmGripController::ArmGripController(IMotionDriver* driver,
                                     const ArmGripConfig& config,
                                     QObject* parent)
    : BaseMechanismController("ArmGrip", driver, parent)
    , m_config(config)
    , m_clampState(ClampState::Unknown)
    , m_isInitializing(false)
    , m_lastPosition(0.0)
    , m_stableCount(0)
    , m_currentDAC(0.0)
{
    m_initTimer = new QTimer(this);
    connect(m_initTimer, &QTimer::timeout, this, &ArmGripController::monitorInit);

    qDebug() << QString("[%1] ArmGripController created, motor_id=%2")
                .arg(mechanismCodeString())
                .arg(m_config.motor.motorId);
}

ArmGripController::~ArmGripController()
{
    stop();
}

bool ArmGripController::initialize()
{
    setState(MechanismState::Initializing, "Initializing arm grip (Mg)");

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

    // 2. 设置为位置模式（默认）
    if (!driver()->setAxisType(motorId, static_cast<int>(MotorMode::Position))) {
        setError("Failed to set position mode");
        return false;
    }

    // 3. 设置运动参数
    driver()->setSpeed(motorId, m_config.motor.defaultSpeed);
    driver()->setAcceleration(motorId, m_config.motor.acceleration);
    driver()->setDeceleration(motorId, m_config.motor.deceleration);

    reportProgress(100, "Initialization complete");

    m_clampState = ClampState::Unknown;
    setState(MechanismState::Ready, "Arm grip (Mg) ready");
    emit initialized();
    return true;
}

bool ArmGripController::stop()
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
        setState(MechanismState::Holding, "Stopped");
    }

    return success;
}

bool ArmGripController::reset()
{
    stop();
    m_clampState = ClampState::Unknown;
    emit clampStateChanged(m_clampState);
    setState(MechanismState::Ready, "Reset complete");
    return true;
}

void ArmGripController::updateStatus()
{
    if (!checkDriver()) {
        return;
    }

    double pos = currentPosition();
    emit positionChanged(pos);
}

// ============================================================================
// 夹爪控制
// ============================================================================

bool ArmGripController::open()
{
    if (!checkDriver() || !isReady()) {
        setError("Controller not ready");
        return false;
    }

    int motorId = m_config.motor.motorId;

    // 切换到力矩模式
    driver()->setAxisType(motorId, static_cast<int>(MotorMode::Torque));

    // 施加张开方向力矩
    driver()->setDAC(motorId, m_config.openDAC);

    m_clampState = ClampState::Opening;
    setState(MechanismState::Moving, "Opening grip");
    emit clampStateChanged(m_clampState);

    qDebug() << QString("[%1] Opening grip, DAC=%2")
                .arg(mechanismCodeString()).arg(m_config.openDAC);

    // 延时后设为Open状态
    QTimer::singleShot(1000, this, [this]() {
        if (m_clampState == ClampState::Opening) {
            m_clampState = ClampState::Open;
            setState(MechanismState::Ready, "Grip opened");
            emit clampStateChanged(m_clampState);
        }
    });

    return true;
}

bool ArmGripController::close(double torque)
{
    if (!checkDriver() || !isReady()) {
        setError("Controller not ready");
        return false;
    }

    int motorId = m_config.motor.motorId;

    // 使用指定力矩或默认力矩
    double closeTorque = (torque > 0) ? torque : m_config.closeDAC;

    // 切换到力矩模式
    driver()->setAxisType(motorId, static_cast<int>(MotorMode::Torque));

    // 施加夹紧方向力矩
    driver()->setDAC(motorId, closeTorque);

    m_clampState = ClampState::Closing;
    setState(MechanismState::Moving, "Closing grip");
    emit clampStateChanged(m_clampState);

    qDebug() << QString("[%1] Closing grip with torque %2")
                .arg(mechanismCodeString()).arg(closeTorque);

    // 延时后锁定位置
    QTimer::singleShot(1000, this, [this, motorId]() {
        if (m_clampState == ClampState::Closing) {
            // 切换到位置模式锁定位置
            double currentPos = driver()->getActualPosition(motorId);
            driver()->setAxisType(motorId, static_cast<int>(MotorMode::Position));
            driver()->setTargetPosition(motorId, currentPos);

            m_clampState = ClampState::Closed;
            setState(MechanismState::Holding, "Grip closed");
            emit clampStateChanged(m_clampState);
        }
    });

    return true;
}

bool ArmGripController::setTorque(double dac)
{
    if (!checkDriver()) {
        return false;
    }

    int motorId = m_config.motor.motorId;

    // 确保是力矩模式
    driver()->setAxisType(motorId, static_cast<int>(MotorMode::Torque));

    if (!driver()->setDAC(motorId, dac)) {
        setError("Failed to set torque");
        return false;
    }

    m_currentDAC = dac;
    qDebug() << QString("[%1] Torque set to %2")
                .arg(mechanismCodeString()).arg(dac);

    return true;
}

double ArmGripController::currentPosition() const
{
    if (!checkDriver()) {
        return 0.0;
    }

    int motorId = m_config.motor.motorId;
    return driver()->getActualPosition(motorId);
}

// ============================================================================
// 初始化
// ============================================================================

bool ArmGripController::initializeGrip()
{
    if (!checkDriver() || !isReady()) {
        setError("Controller not ready");
        return false;
    }

    setState(MechanismState::Initializing, "Finding grip home position");

    int motorId = m_config.motor.motorId;

    // 切换到力矩模式
    driver()->setAxisType(motorId, static_cast<int>(MotorMode::Torque));

    // 从小力矩开始
    m_currentDAC = m_config.initDAC;
    driver()->setDAC(motorId, m_currentDAC);

    // 启动监控
    m_isInitializing = true;
    m_lastPosition = driver()->getActualPosition(motorId);
    m_stableCount = 0;

    m_initTimer->start(m_config.monitorInterval);

    qDebug() << QString("[%1] Grip initialization started")
                .arg(mechanismCodeString());

    return true;
}

void ArmGripController::monitorInit()
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

            m_clampState = ClampState::Closed;

            setState(MechanismState::Ready, "Grip initialized");
            emit clampStateChanged(m_clampState);

            qDebug() << QString("[%1] Grip initialization completed")
                        .arg(mechanismCodeString());
        }
    } else {
        m_stableCount = 0;
        m_lastPosition = currentPos;

        // 逐渐增大力矩
        if (m_currentDAC < m_config.maxDAC) {
            m_currentDAC += m_config.dacIncrement;
            driver()->setDAC(motorId, m_currentDAC);
        }
    }
}

// ============================================================================
// 关键位置
// ============================================================================

double ArmGripController::getKeyPosition(const QString& key) const
{
    if (m_config.keyPositions.contains(key)) {
        return m_config.keyPositions.value(key);
    }
    return 0.0;  // DAC默认返回0
}

bool ArmGripController::applyKeyTorque(const QString& key)
{
    double dac = getKeyPosition(key);

    if (!checkDriver() || !isReady()) {
        setError("Controller not ready");
        return false;
    }

    qDebug() << QString("[%1] Applying key torque %2: DAC=%3")
                .arg(mechanismCodeString()).arg(key).arg(dac);

    // 根据DAC方向决定动作
    if (dac < 0) {
        // 张开方向
        return open();
    } else if (dac > 0) {
        // 夹紧方向
        return close(dac);
    } else {
        // DAC=0，停止
        return stop();
    }
}

QStringList ArmGripController::keyPositionNames() const
{
    return m_config.keyPositions.keys();
}

// ============================================================================
// 配置热更新
// ============================================================================

void ArmGripController::updateConfig(const ArmGripConfig& config)
{
    qDebug() << QString("[%1] Updating config").arg(mechanismCodeString());
    m_config = config;
    qDebug() << QString("[%1] Config updated").arg(mechanismCodeString());
}
