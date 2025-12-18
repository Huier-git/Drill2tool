#ifndef MOTIONLOCKMANAGER_H
#define MOTIONLOCKMANAGER_H

#include <QObject>
#include <QMutex>
#include <QString>

/**
 * @brief 运动来源类型
 */
enum class MotionSource {
    None,           // 空闲
    ManualJog,      // 手动点动
    ManualAbs,      // 手动绝对运动
    AutoScript,     // 自动脚本（机构控制器）
    Homing          // 回零
};

/**
 * @brief 运动互锁管理器（单例）
 *
 * 职责：
 * 1. 管理运动控制的互斥访问
 * 2. 检测运动冲突并弹窗警告
 * 3. 冲突确认后停止当前运动
 * 4. 提供急停功能
 *
 * 规则：
 * - 数据采集（只读）不受互锁限制
 * - 运动控制之间互斥
 * - 新运动请求时，若有冲突则弹窗确认
 * - 确认后先 StopAll，再执行新运动
 * - 急停无条件立即执行
 */
class MotionLockManager : public QObject
{
    Q_OBJECT

public:
    static MotionLockManager* instance();

    /**
     * @brief 请求运动许可
     * @param source 运动来源
     * @param description 运动描述（用于弹窗显示）
     * @return true=获得许可，false=被拒绝或取消
     *
     * 注意：此函数可能弹出对话框，必须在主线程调用
     */
    bool requestMotion(MotionSource source, const QString& description);

    /**
     * @brief 释放运动许可
     * @param source 运动来源（必须与请求时一致）
     */
    void releaseMotion(MotionSource source);

    /**
     * @brief 急停 - 无条件停止所有运动
     */
    void emergencyStop();

    /**
     * @brief 获取当前运动来源
     */
    MotionSource currentSource() const;

    /**
     * @brief 获取当前运动描述
     */
    QString currentDescription() const;

    /**
     * @brief 检查是否空闲
     */
    bool isIdle() const;

    /**
     * @brief 获取运动来源的字符串描述
     */
    static QString sourceToString(MotionSource source);

signals:
    /**
     * @brief 运动冲突信号
     */
    void motionConflict(MotionSource current, MotionSource requested);

    /**
     * @brief 急停触发信号
     */
    void emergencyStopTriggered();

    /**
     * @brief 运动状态改变信号
     */
    void motionStateChanged(MotionSource source, const QString& description);

private:
    explicit MotionLockManager(QObject* parent = nullptr);
    ~MotionLockManager() = default;

    // 禁止拷贝
    MotionLockManager(const MotionLockManager&) = delete;
    MotionLockManager& operator=(const MotionLockManager&) = delete;

    // 执行停止所有电机（内部使用）
    void doStopAll();

    // 显示冲突确认对话框
    bool showConflictDialog(MotionSource current, MotionSource requested,
                            const QString& currentDesc, const QString& newDesc);

private:
    mutable QMutex m_mutex;
    MotionSource m_currentSource;
    QString m_currentDescription;

    static MotionLockManager* s_instance;
};

#endif // MOTIONLOCKMANAGER_H
