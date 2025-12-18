#ifndef ARMEXTENSIONCONTROLLER_H
#define ARMEXTENSIONCONTROLLER_H

#include "control/BaseMechanismController.h"
#include "control/MechanismTypes.h"
#include "control/MechanismDefs.h"
#include <QTimer>

/**
 * @brief 机械手伸缩配置
 */
struct ArmExtensionConfig {
    MotorConfig motor;
    double extendPosition = 50000.0;    // 伸出位置
    double retractPosition = 0.0;       // 回收位置
    double initDAC = -50.0;             // 初始化力矩
    double stableThreshold = 1.0;       // 位置稳定阈值
    int stableCount = 5;                // 稳定计数阈值
    int monitorInterval = 200;          // 监控间隔(ms)

    // 关键位置 (A=完全收回, B=面对存储机构, C=面对对接头)
    QMap<QString, double> keyPositions;

    static ArmExtensionConfig fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
};

/**
 * @brief 机械手伸缩控制器 (Me)
 *
 * 功能：
 * 1. 伸缩位置控制
 * 2. 找零点初始化
 * 3. 伸出/回收快捷操作
 *
 * 机构代号: Me (Manipulator Extension)
 * 电机索引: 6
 */
class ArmExtensionController : public BaseMechanismController
{
    Q_OBJECT

public:
    static constexpr Mechanism::Code MechanismCode = Mechanism::Me;

    explicit ArmExtensionController(IMotionDriver* driver,
                                   const ArmExtensionConfig& config,
                                   QObject* parent = nullptr);
    ~ArmExtensionController() override;

    // BaseMechanismController接口
    bool initialize() override;
    bool stop() override;
    bool reset() override;
    void updateStatus() override;

    // ========================================================================
    // 伸缩控制
    // ========================================================================

    /**
     * @brief 设置伸出长度/位置
     */
    bool setPosition(double position);

    /**
     * @brief 伸出到最大位置
     */
    bool extend();

    /**
     * @brief 回收到最小位置
     */
    bool retract();

    /**
     * @brief 获取当前位置
     */
    double currentPosition() const;

    /**
     * @brief 是否正在运动
     */
    bool isMoving() const { return m_isMoving; }

    // ========================================================================
    // 初始化
    // ========================================================================

    /**
     * @brief 找零点初始化
     */
    bool initializePosition();

    /**
     * @brief 重置零点
     */
    bool resetZero();

    // ========================================================================
    // 关键位置
    // ========================================================================

    /**
     * @brief 获取关键位置脉冲值
     * @param key 位置代号 (A-C)
     * @return 脉冲值，不存在返回-1
     */
    double getKeyPosition(const QString& key) const;

    /**
     * @brief 移动到关键位置
     * @param key 位置代号 (A-C)
     * @return 成功返回true
     */
    bool moveToKeyPosition(const QString& key);

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
    void updateConfig(const ArmExtensionConfig& config);

signals:
    void positionChanged(double position);
    void targetReached();

private slots:
    void monitorInit();

private:
    ArmExtensionConfig m_config;

    double m_offset;            // 零点偏移
    bool m_isMoving;
    bool m_isInitializing;
    double m_lastPosition;
    int m_stableCount;
    QTimer* m_initTimer;
};

#endif // ARMEXTENSIONCONTROLLER_H
