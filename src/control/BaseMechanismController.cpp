#include "control/BaseMechanismController.h"
#include "Logger.h"
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
            LOG_DEBUG_STREAM(m_name.toStdString().c_str())
                << "Status update enabled, interval:" << intervalMs << "ms";
        }
    } else {
        if (m_statusUpdateTimer->isActive()) {
            m_statusUpdateTimer->stop();
            LOG_DEBUG(m_name.toStdString().c_str(), "Status update disabled");
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

        LOG_DEBUG_STREAM(m_name.toStdString().c_str())
            << "State:" << mechanismStateToString(oldState)
            << "->" << mechanismStateToString(newState);

        if (!message.isEmpty()) {
            LOG_DEBUG_STREAM(m_name.toStdString().c_str()) << message;
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

    LOG_ERROR_STREAM(m_name.toStdString().c_str()) << "ERROR:" << errorMessage;
    emit errorOccurred(errorMessage);
}

void BaseMechanismController::reportProgress(int percent, const QString& message)
{
    m_progress = qBound(0, percent, 100);

    LOG_DEBUG_STREAM(m_name.toStdString().c_str())
        << "Progress:" << m_progress << "% -" << message;

    emit progressUpdated(m_progress, message);
}

bool BaseMechanismController::checkDriver() const
{
    if (!m_driver) {
        LOG_WARNING_STREAM(m_name.toStdString().c_str()) << "Driver is null";
        return false;
    }

    if (!m_driver->isConnected()) {
        LOG_WARNING_STREAM(m_name.toStdString().c_str()) << "Driver not connected";
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
        LOG_DEBUG(m_name.toStdString().c_str(), "Already has motion lock");
        return true;
    }

    QString fullDescription = QString("%1: %2").arg(m_name).arg(description);

    bool granted = MotionLockManager::instance()->requestMotion(
        MotionSource::AutoScript, fullDescription);

    if (granted) {
        m_hasMotionLock = true;
        LOG_DEBUG_STREAM(m_name.toStdString().c_str())
            << "Motion lock acquired:" << description;
    } else {
        LOG_DEBUG_STREAM(m_name.toStdString().c_str())
            << "Motion lock denied:" << description;
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
    LOG_DEBUG(m_name.toStdString().c_str(), "Motion lock released");
}
