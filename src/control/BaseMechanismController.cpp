#include "control/BaseMechanismController.h"
#include "control/MotionLockManager.h"
#include <QDebug>

BaseMechanismController::BaseMechanismController(const QString& name,
                                                 IMotionDriver* driver,
                                                 QObject* parent)
    : QObject(parent)
    , m_name(name)
    , m_driver(driver)
    , m_state(MechanismState::Uninitialized)
    , m_progress(0)
    , m_statusUpdateTimer(nullptr)
    , m_hasMotionLock(false)
{
    // 创建状态更新定时器（默认不启动）
    m_statusUpdateTimer = new QTimer(this);
    connect(m_statusUpdateTimer, &QTimer::timeout, this, &BaseMechanismController::updateStatus);
}

BaseMechanismController::~BaseMechanismController()
{
    // 确保释放运动锁
    if (m_hasMotionLock) {
        releaseMotionLock();
    }

    if (m_statusUpdateTimer) {
        m_statusUpdateTimer->stop();
    }
}

// ============================================================================
// 通用接口实现
// ============================================================================

bool BaseMechanismController::isReady() const
{
    return m_state == MechanismState::Ready || m_state == MechanismState::Holding;
}

QString BaseMechanismController::stateString() const
{
    return mechanismStateToString(m_state);
}

MechanismStatus BaseMechanismController::getStatus() const
{
    MechanismStatus status;
    status.mechanismName = m_name;
    status.state = m_state;
    status.stateMessage = m_stateMessage;
    status.progress = m_progress;
    status.hasError = (m_state == MechanismState::Error);
    status.errorMessage = m_errorMessage;
    return status;
}

void BaseMechanismController::setStatusUpdateEnabled(bool enable, int intervalMs)
{
    if (enable) {
        if (!m_statusUpdateTimer->isActive()) {
            m_statusUpdateTimer->start(intervalMs);
            qDebug() << QString("[%1] Status update enabled, interval: %2ms")
                        .arg(m_name).arg(intervalMs);
        }
    } else {
        if (m_statusUpdateTimer->isActive()) {
            m_statusUpdateTimer->stop();
            qDebug() << QString("[%1] Status update disabled").arg(m_name);
        }
    }
}

// ============================================================================
// 受保护的辅助方法
// ============================================================================

void BaseMechanismController::setState(MechanismState newState, const QString& message)
{
    if (m_state != newState) {
        MechanismState oldState = m_state;
        m_state = newState;
        m_stateMessage = message;
        
        // 清除错误信息（如果状态不是Error）
        if (newState != MechanismState::Error) {
            m_errorMessage.clear();
        }
        
        qDebug() << QString("[%1] State: %2 -> %3")
                    .arg(m_name)
                    .arg(mechanismStateToString(oldState))
                    .arg(mechanismStateToString(newState));
        
        if (!message.isEmpty()) {
            qDebug() << QString("[%1] %2").arg(m_name).arg(message);
        }
        
        emit stateChanged(newState, message);
        
        // 特殊状态的信号
        if (newState == MechanismState::Ready && oldState == MechanismState::Initializing) {
            emit initialized();
        }
    }
}

void BaseMechanismController::setError(const QString& errorMessage)
{
    m_errorMessage = errorMessage;
    setState(MechanismState::Error, errorMessage);
    
    qWarning() << QString("[%1] ERROR: %2").arg(m_name).arg(errorMessage);
    emit errorOccurred(errorMessage);
}

void BaseMechanismController::reportProgress(int percent, const QString& message)
{
    m_progress = qBound(0, percent, 100);
    
    qDebug() << QString("[%1] Progress: %2% - %3")
                .arg(m_name)
                .arg(m_progress)
                .arg(message);
    
    emit progressUpdated(m_progress, message);
}

bool BaseMechanismController::checkDriver() const
{
    if (!m_driver) {
        qWarning() << QString("[%1] Driver is null").arg(m_name);
        return false;
    }

    if (!m_driver->isConnected()) {
        qWarning() << QString("[%1] Driver not connected").arg(m_name);
        return false;
    }

    return true;
}

// ============================================================================
// 运动互锁方法
// ============================================================================

bool BaseMechanismController::requestMotionLock(const QString& description)
{
    if (m_hasMotionLock) {
        // 已经持有锁，直接返回
        qDebug() << QString("[%1] Already has motion lock").arg(m_name);
        return true;
    }

    QString fullDescription = QString("%1: %2").arg(m_name).arg(description);

    bool granted = MotionLockManager::instance()->requestMotion(
        MotionSource::AutoScript, fullDescription);

    if (granted) {
        m_hasMotionLock = true;
        qDebug() << QString("[%1] Motion lock acquired: %2").arg(m_name).arg(description);
    } else {
        qDebug() << QString("[%1] Motion lock denied: %2").arg(m_name).arg(description);
    }

    return granted;
}

void BaseMechanismController::releaseMotionLock()
{
    if (!m_hasMotionLock) {
        return;
    }

    MotionLockManager::instance()->releaseMotion(MotionSource::AutoScript);
    m_hasMotionLock = false;
    qDebug() << QString("[%1] Motion lock released").arg(m_name);
}
