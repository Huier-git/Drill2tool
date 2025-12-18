#ifndef BASEMECHANISMCONTROLLER_H
#define BASEMECHANISMCONTROLLER_H

#include "control/MechanismTypes.h"
#include "control/IMotionDriver.h"
#include <QObject>
#include <QTimer>

/**
 * @brief 机构控制器基类
 *
 * 为所有机构控制器提供通用功能：
 * 1. 状态管理
 * 2. 错误处理
 * 3. 初始化框架
 * 4. 驱动接口管理
 * 5. 运动互锁管理（通过 MotionLockManager）
 *
 * 子类需要实现：
 * - initialize() - 机构初始化逻辑
 * - stop() - 停止运动
 * - reset() - 复位到初始状态
 * - updateStatus() - 更新机构状态
 *
 * 运动互锁：
 * - 子类在执行运动前应调用 requestMotionLock()
 * - 运动完成后调用 releaseMotionLock()
 * - 互锁由 MotionLockManager 统一管理
 */
class BaseMechanismController : public QObject
{
    Q_OBJECT

public:
    explicit BaseMechanismController(const QString& name,
                                     IMotionDriver* driver,
                                     QObject* parent = nullptr);
    virtual ~BaseMechanismController();

    // ========================================================================
    // 纯虚函数 - 子类必须实现
    // ========================================================================

    virtual bool initialize() = 0;
    virtual bool stop() = 0;
    virtual bool reset() = 0;
    virtual void updateStatus() = 0;

    // ========================================================================
    // 通用接口
    // ========================================================================

    virtual bool isReady() const;
    MechanismState state() const { return m_state; }
    QString stateString() const;
    QString name() const { return m_name; }
    MechanismStatus getStatus() const;
    void setStatusUpdateEnabled(bool enable, int intervalMs = 100);

signals:
    void stateChanged(MechanismState newState, const QString& message);
    void errorOccurred(const QString& error);
    void progressUpdated(int percent, const QString& message);
    void initialized();
    void movementCompleted();

protected:
    // ========================================================================
    // 受保护的辅助方法
    // ========================================================================

    void setState(MechanismState newState, const QString& message = QString());
    void setError(const QString& errorMessage);
    void reportProgress(int percent, const QString& message);
    IMotionDriver* driver() { return m_driver; }
    const IMotionDriver* driver() const { return m_driver; }
    bool checkDriver() const;

    // ========================================================================
    // 运动互锁方法
    // ========================================================================

    /**
     * @brief 请求运动互锁
     * @param description 运动描述（用于冲突弹窗显示）
     * @return true=获得许可，false=被拒绝或用户取消
     *
     * 子类在执行任何运动操作前应调用此方法。
     * 如果当前有其他运动正在进行，会弹窗询问用户是否打断。
     */
    bool requestMotionLock(const QString& description);

    /**
     * @brief 释放运动互锁
     *
     * 子类在运动完成后应调用此方法。
     */
    void releaseMotionLock();

    /**
     * @brief 检查是否持有运动互锁
     */
    bool hasMotionLock() const { return m_hasMotionLock; }

protected:
    QString m_name;
    IMotionDriver* m_driver;
    MechanismState m_state;
    QString m_stateMessage;
    QString m_errorMessage;
    int m_progress;

    QTimer* m_statusUpdateTimer;
    bool m_hasMotionLock;           // 是否持有运动锁
};

#endif // BASEMECHANISMCONTROLLER_H
