#include "control/MotionLockManager.h"
#include "Global.h"
#include "control/zmotion.h"
#include "control/zmcaux.h"
#include "qabstractbutton.h"
#include <QMessageBox>
#include <QDebug>
#include <QApplication>

// 静态实例
MotionLockManager* MotionLockManager::s_instance = nullptr;

MotionLockManager* MotionLockManager::instance()
{
    if (!s_instance) {
        s_instance = new MotionLockManager();
    }
    return s_instance;
}

MotionLockManager::MotionLockManager(QObject* parent)
    : QObject(parent)
    , m_currentSource(MotionSource::None)
{
    qDebug() << "[MotionLockManager] Initialized";
}

bool MotionLockManager::requestMotion(MotionSource source, const QString& description)
{
    QMutexLocker locker(&m_mutex);

    // 如果当前空闲，直接获得许可
    if (m_currentSource == MotionSource::None) {
        m_currentSource = source;
        m_currentDescription = description;
        qDebug() << "[MotionLockManager] Motion granted:" << sourceToString(source) << "-" << description;
        emit motionStateChanged(source, description);
        return true;
    }

    // 同类型的手动点动，直接覆盖（允许连续点动）
    if (m_currentSource == MotionSource::ManualJog && source == MotionSource::ManualJog) {
        m_currentDescription = description;
        qDebug() << "[MotionLockManager] ManualJog updated:" << description;
        return true;
    }

    // 有冲突，需要用户确认
    QString currentDesc = m_currentDescription;
    MotionSource currentSrc = m_currentSource;

    // 解锁后显示对话框（避免死锁）
    locker.unlock();

    emit motionConflict(currentSrc, source);

    bool confirmed = showConflictDialog(currentSrc, source, currentDesc, description);

    if (!confirmed) {
        qDebug() << "[MotionLockManager] Motion request cancelled by user";
        return false;
    }

    // 用户确认，先停止所有运动
    qDebug() << "[MotionLockManager] User confirmed, stopping current motion...";
    doStopAll();

    // 重新获取锁，设置新状态
    locker.relock();
    m_currentSource = source;
    m_currentDescription = description;
    qDebug() << "[MotionLockManager] Motion granted after stop:" << sourceToString(source) << "-" << description;
    emit motionStateChanged(source, description);
    return true;
}

void MotionLockManager::releaseMotion(MotionSource source)
{
    QMutexLocker locker(&m_mutex);

    if (m_currentSource == source) {
        qDebug() << "[MotionLockManager] Motion released:" << sourceToString(source);
        m_currentSource = MotionSource::None;
        m_currentDescription.clear();
        emit motionStateChanged(MotionSource::None, QString());
    }
}

void MotionLockManager::emergencyStop()
{
    qWarning() << "[MotionLockManager] EMERGENCY STOP TRIGGERED!";

    // 立即停止，不需要用户确认
    doStopAll();

    // 清除状态
    {
        QMutexLocker locker(&m_mutex);
        m_currentSource = MotionSource::None;
        m_currentDescription.clear();
    }

    emit emergencyStopTriggered();
    emit motionStateChanged(MotionSource::None, QString());
}

MotionSource MotionLockManager::currentSource() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentSource;
}

QString MotionLockManager::currentDescription() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentDescription;
}

bool MotionLockManager::isIdle() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentSource == MotionSource::None;
}

QString MotionLockManager::sourceToString(MotionSource source)
{
    switch (source) {
        case MotionSource::None:       return "空闲";
        case MotionSource::ManualJog:  return "手动点动";
        case MotionSource::ManualAbs:  return "手动定位";
        case MotionSource::AutoScript: return "自动脚本";
        case MotionSource::Homing:     return "回零";
        default:                       return "未知";
    }
}

void MotionLockManager::doStopAll()
{
    QMutexLocker locker(&g_mutex);

    if (!g_handle) {
        qWarning() << "[MotionLockManager] Cannot stop: no handle";
        return;
    }

    // 急停模式2：取消缓冲和当前运动
    int result = ZAux_Direct_Rapidstop(g_handle, 2);
    if (result != 0) {
        qWarning() << "[MotionLockManager] Rapidstop failed, error:" << result;
    } else {
        qDebug() << "[MotionLockManager] All motors stopped";
    }
}

bool MotionLockManager::showConflictDialog(MotionSource current, MotionSource requested,
                                            const QString& currentDesc, const QString& newDesc)
{
    QString message = QString(
        "检测到运动冲突！\n\n"
        "当前运动：%1\n"
        "  描述：%2\n\n"
        "请求运动：%3\n"
        "  描述：%4\n\n"
        "是否停止当前运动并执行新操作？"
    ).arg(sourceToString(current))
     .arg(currentDesc.isEmpty() ? "(无)" : currentDesc)
     .arg(sourceToString(requested))
     .arg(newDesc.isEmpty() ? "(无)" : newDesc);

    QMessageBox msgBox;
    msgBox.setWindowTitle("运动冲突警告");
    msgBox.setText(message);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    msgBox.button(QMessageBox::Yes)->setText("停止并执行");
    msgBox.button(QMessageBox::No)->setText("取消");

    return msgBox.exec() == QMessageBox::Yes;
}
