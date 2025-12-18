#ifndef ARMGRIPCONTROLLER_H
#define ARMGRIPCONTROLLER_H

#include "control/BaseMechanismController.h"
#include "control/MechanismTypes.h"
#include "control/MechanismDefs.h"
#include <QTimer>

/**
 * @brief 机械手夹紧配置
 */
struct ArmGripConfig {
    MotorConfig motor;
    double openDAC = -100.0;            // 张开力矩
    double closeDAC = 100.0;            // 闭合力矩
    double initDAC = 10.0;              // 初始化起始力矩
    double maxDAC = 80.0;               // 最大力矩
    double dacIncrement = 10.0;         // 力矩增量
    double stableThreshold = 1.0;       // 位置稳定阈值
    int stableCount = 5;                // 稳定计数阈值
    int monitorInterval = 200;          // 监控间隔(ms)

    // 关键位置 (A=完全张开, B=完全夹紧) - DAC值
    QMap<QString, double> keyPositions;

    static ArmGripConfig fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
};

/**
 * @brief 机械手夹紧控制器 (Mg)
 *
 * 功能：
 * 1. 夹爪张开/闭合控制（力矩模式）
 * 2. 找零点初始化
 * 3. 夹紧力矩可调
 *
 * 机构代号: Mg (Manipulator Grip)
 * 电机索引: 4
 */
class ArmGripController : public BaseMechanismController
{
    Q_OBJECT

public:
    static constexpr Mechanism::Code MechanismCode = Mechanism::Mg;

    explicit ArmGripController(IMotionDriver* driver,
                              const ArmGripConfig& config,
                              QObject* parent = nullptr);
    ~ArmGripController() override;

    // BaseMechanismController接口
    bool initialize() override;
    bool stop() override;
    bool reset() override;
    void updateStatus() override;

    // ========================================================================
    // 夹爪控制
    // ========================================================================

    /**
     * @brief 打开夹爪
     */
    bool open();

    /**
     * @brief 闭合夹爪
     * @param torque 夹紧力矩（可选）
     */
    bool close(double torque = -1);

    /**
     * @brief 获取夹爪状态
     */
    ClampState clampState() const { return m_clampState; }

    /**
     * @brief 设置力矩
     */
    bool setTorque(double dac);

    /**
     * @brief 获取当前位置
     */
    double currentPosition() const;

    // ========================================================================
    // 初始化
    // ========================================================================

    /**
     * @brief 找零点初始化
     */
    bool initializeGrip();

    // ========================================================================
    // 关键位置
    // ========================================================================

    /**
     * @brief 获取关键位置DAC值
     * @param key 位置代号 (A-B)
     * @return DAC值，不存在返回0
     */
    double getKeyPosition(const QString& key) const;

    /**
     * @brief 应用关键位置的力矩
     * @param key 位置代号 (A-B)
     * @return 成功返回true
     */
    bool applyKeyTorque(const QString& key);

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
    void updateConfig(const ArmGripConfig& config);

signals:
    void clampStateChanged(ClampState state);
    void positionChanged(double position);

private slots:
    void monitorInit();

private:
    ArmGripConfig m_config;

    ClampState m_clampState;
    bool m_isInitializing;
    double m_lastPosition;
    int m_stableCount;
    double m_currentDAC;
    QTimer* m_initTimer;
};

#endif // ARMGRIPCONTROLLER_H
