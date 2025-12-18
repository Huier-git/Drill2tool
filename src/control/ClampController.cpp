#include "control/ClampController.h"
#include <QDebug>
#include <cmath>

ClampController::ClampController(IMotionDriver* driver,
                                 const ClampConfig& config,
                                 QObject* parent)
    : BaseMechanismController("Clamp", driver, parent)
    , m_config(config)
    , m_state(ClampState::Unknown)
    , m_torque(config.closeDAC)
    , m_isInitializing(false)
    , m_lastPosition(0.0)
    , m_stableCount(0)
{
    m_initTimer = new QTimer(this);
    connect(m_initTimer, &QTimer::timeout, this, &ClampController::monitorInit);

    qDebug() << QString("[%1] ClampController created, motor_id=%2")
                .arg(mechanismCodeString())
                .arg(m_config.motor.motorId);
}

ClampController::~ClampController()
{
    stop();
}

bool ClampController::initialize()
{
    setState(MechanismState::Initializing, "Initializing clamp (Cb)");

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
    
    m_state = ClampState::Unknown;
    setState(MechanismState::Ready, "Clamp (Cb) ready");

    emit initialized();
    return true;
}

bool ClampController::stop()
{
    if (!checkDriver()) {
        return false;
    }
    
    int motorId = m_config.motor.motorId;
    bool success = driver()->stopAxis(motorId, 2);
    
    if (m_initTimer->isActive()) {
        m_initTimer->stop();
    }
    
    if (success) {
        setState(MechanismState::Holding, "Stopped");
    }
    
    return success;
}

bool ClampController::reset()
{
    stop();
    m_state = ClampState::Unknown;
    setState(MechanismState::Ready, "Reset complete");
    return true;
}

void ClampController::updateStatus()
{
    // 状态更新
}

bool ClampController::open()
{
    if (!checkDriver() || !isReady()) {
        setError("Controller not ready");
        return false;
    }
    
    int motorId = m_config.motor.motorId;
    
    // 切换到力矩模式
    driver()->setAxisType(motorId, static_cast<int>(MotorMode::Torque));
    driver()->setDAC(motorId, m_config.openDAC);
    
    m_state = ClampState::Opening;
    setState(MechanismState::Moving, "Opening clamp");
    emit stateChanged(m_state);

    qDebug() << QString("[%1] Opening").arg(mechanismCodeString());
    
    // 简化：延时后切换状态
    QTimer::singleShot(1000, this, [this]() {
        if (m_state == ClampState::Opening) {
            m_state = ClampState::Open;
            setState(MechanismState::Ready, "Clamp opened");
            emit stateChanged(m_state);
        }
    });
    
    return true;
}

bool ClampController::close(double torque)
{
    if (!checkDriver() || !isReady()) {
        setError("Controller not ready");
        return false;
    }
    
    double closeTorque = (torque > 0) ? torque : m_torque;
    int motorId = m_config.motor.motorId;
    
    // 切换到力矩模式
    driver()->setAxisType(motorId, static_cast<int>(MotorMode::Torque));
    driver()->setDAC(motorId, closeTorque);
    
    m_state = ClampState::Closing;
    setState(MechanismState::Moving, "Closing clamp");
    emit stateChanged(m_state);

    qDebug() << QString("[%1] Closing with torque %2")
                .arg(mechanismCodeString()).arg(closeTorque);
    
    // 简化：延时后锁定位置
    QTimer::singleShot(1000, this, [this]() {
        if (m_state == ClampState::Closing) {
            int motorId = m_config.motor.motorId;
            double currentPos = driver()->getActualPosition(motorId);
            
            // 切换到位置模式锁定
            driver()->setAxisType(motorId, static_cast<int>(MotorMode::Position));
            driver()->setTargetPosition(motorId, currentPos);
            
            m_state = ClampState::Closed;
            setState(MechanismState::Holding, "Clamp closed");
            emit stateChanged(m_state);
        }
    });
    
    return true;
}

bool ClampController::initializeClamp()
{
    if (!checkDriver() || !isReady()) {
        setError("Controller not ready");
        return false;
    }
    
    setState(MechanismState::Initializing, "Finding clamp zero point");
    
    int motorId = m_config.motor.motorId;
    
    // 切换到力矩模式
    driver()->setAxisType(motorId, static_cast<int>(MotorMode::Torque));
    driver()->setDAC(motorId, -50.0);  // 反向力矩
    
    m_isInitializing = true;
    m_lastPosition = driver()->getActualPosition(motorId);
    m_stableCount = 0;
    
    m_initTimer->start(200);

    qDebug() << QString("[%1] Initialization started").arg(mechanismCodeString());
    
    return true;
}

void ClampController::monitorInit()
{
    if (!m_isInitializing) {
        m_initTimer->stop();
        return;
    }

    int motorId = m_config.motor.motorId;
    double currentPos = driver()->getActualPosition(motorId);
    double posChange = std::abs(currentPos - m_lastPosition);

    if (posChange < m_config.positionTolerance) {
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

            m_state = ClampState::Open;
            setState(MechanismState::Ready, "Clamp initialized");
            emit stateChanged(m_state);

            qDebug() << QString("[%1] Initialization completed").arg(mechanismCodeString());
        }
    } else {
        m_stableCount = 0;
        m_lastPosition = currentPos;
    }
}

// ============================================================================
// 关键位置
// ============================================================================

double ClampController::getKeyPosition(const QString& key) const
{
    if (m_config.keyPositions.contains(key)) {
        return m_config.keyPositions.value(key);
    }
    return 0.0;  // DAC默认返回0
}

bool ClampController::applyKeyTorque(const QString& key)
{
    double dac = getKeyPosition(key);

    if (!checkDriver() || !isReady()) {
        setError("Controller not ready");
        return false;
    }

    int motorId = m_config.motor.motorId;

    // 切换到力矩模式并应用
    driver()->setAxisType(motorId, static_cast<int>(MotorMode::Torque));
    driver()->setDAC(motorId, dac);

    qDebug() << QString("[%1] Applying key torque %2: DAC=%3")
                .arg(mechanismCodeString()).arg(key).arg(dac);

    // 根据DAC值决定状态
    if (dac < 0) {
        m_state = ClampState::Opening;
    } else if (dac > 0) {
        m_state = ClampState::Closing;
    }
    emit stateChanged(m_state);

    return true;
}

QStringList ClampController::keyPositionNames() const
{
    return m_config.keyPositions.keys();
}

// ============================================================================
// 配置热更新
// ============================================================================

void ClampController::updateConfig(const ClampConfig& config)
{
    qDebug() << QString("[%1] Updating config").arg(mechanismCodeString());
    m_config = config;
    m_torque = config.closeDAC;  // 更新默认夹紧力矩
    qDebug() << QString("[%1] Config updated").arg(mechanismCodeString());
}
