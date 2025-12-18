#ifndef PERCUSSIONCONTROLLER_H
#define PERCUSSIONCONTROLLER_H

#include "control/BaseMechanismController.h"
#include "control/MechanismTypes.h"
#include "control/MechanismDefs.h"
#include <QTimer>
#include <QTime>

/**
 * @brief 冲击控制配置
 */
struct PercussionConfig {
    MotorConfig motor;                  // 电机配置
    double defaultFrequency = 5.0;      // 默认冲击频率(Hz)
    double unlockDAC = -30.0;           // 解锁力矩
    double unlockPosition = -100.0;     // 解锁位置
    int stableTime = 3000;              // 稳定时间(ms)
    double positionTolerance = 1.0;     // 位置容差

    // 关键位置 (A=不冲击, B=程序调控冲击)
    QMap<QString, double> keyPositions;

    static PercussionConfig fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
};

/**
 * @brief 冲击控制器 (Pi)
 *
 * 功能：
 * 1. 冲击频率控制
 * 2. 冲击锁定/解锁机制
 * 3. 位置模式/速度模式/力矩模式切换
 *
 * 机构代号: Pi (Percussion Impact)
 * 电机索引: 1
 */
class PercussionController : public BaseMechanismController
{
    Q_OBJECT

public:
    // 机构代号
    static constexpr Mechanism::Code MechanismCode = Mechanism::Pi;

    explicit PercussionController(IMotionDriver* driver,
                                 const PercussionConfig& config,
                                 QObject* parent = nullptr);
    ~PercussionController() override;

    // ========================================================================
    // BaseMechanismController接口实现
    // ========================================================================

    bool initialize() override;
    bool stop() override;
    bool reset() override;
    void updateStatus() override;

    // ========================================================================
    // 冲击控制
    // ========================================================================

    /**
     * @brief 开始冲击
     * @param frequency 冲击频率（Hz）
     * @return 成功返回true
     */
    bool startPercussion(double frequency = -1);

    /**
     * @brief 停止冲击
     */
    bool stopPercussion();

    /**
     * @brief 设置冲击频率
     * @param frequency 频率（Hz）
     */
    bool setFrequency(double frequency);

    /**
     * @brief 检查是否正在冲击
     */
    bool isPercussing() const { return m_isPercussing; }

    /**
     * @brief 获取当前冲击频率
     */
    double frequency() const { return m_frequency; }

    // ========================================================================
    // 冲击锁定控制
    // ========================================================================

    /**
     * @brief 解锁冲击电机
     * 使用力矩模式将冲击电机反向转动到解锁位置
     */
    bool unlock();

    /**
     * @brief 锁定冲击电机
     */
    bool lock();

    /**
     * @brief 检查冲击电机是否锁定
     */
    bool isLocked() const { return m_isLocked; }

    /**
     * @brief 是否正在解锁过程中
     */
    bool isUnlocking() const { return m_isUnlocking; }

    // ========================================================================
    // 关键位置
    // ========================================================================

    /**
     * @brief 获取关键位置值（频率值）
     * @param key 位置代号 (A-B)
     * @return 频率值，不存在返回-1
     */
    double getKeyPosition(const QString& key) const;

    /**
     * @brief 应用关键位置的频率
     * @param key 位置代号 (A-B)
     * @return 成功返回true
     */
    bool applyKeyFrequency(const QString& key);

    /**
     * @brief 获取所有关键位置名称
     */
    QStringList keyPositionNames() const;

    // ========================================================================
    // 机构代号接口
    // ========================================================================

    Mechanism::Code mechanismCode() const { return MechanismCode; }
    QString mechanismCodeString() const { return Mechanism::getCodeString(MechanismCode); }

    /**
     * @brief 更新配置（热更新）
     */
    void updateConfig(const PercussionConfig& config);

signals:
    /**
     * @brief 冲击状态变化信号
     */
    void percussionStateChanged(bool isPercussing, double frequency);

    /**
     * @brief 锁定状态变化信号
     */
    void lockStateChanged(bool isLocked);

    /**
     * @brief 解锁完成信号
     */
    void unlockCompleted(bool success);

private slots:
    void monitorUnlock();
    void onUnlockTimeout();

private:
    // 频率到速度的转换
    double frequencyToSpeed(double frequency) const;

    PercussionConfig m_config;

    bool m_isPercussing;
    double m_frequency;         // 冲击频率(Hz)
    bool m_isLocked;            // 是否锁定

    // 解锁状态
    bool m_isUnlocking;
    double m_lastPosition;
    QTime m_stableStartTime;
    QTimer* m_unlockMonitorTimer;
    QTimer* m_unlockTimeoutTimer;
};

#endif // PERCUSSIONCONTROLLER_H
