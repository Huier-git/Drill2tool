#ifndef ROTATIONCONTROLLER_H
#define ROTATIONCONTROLLER_H

#include "control/BaseMechanismController.h"
#include "control/MechanismTypes.h"
#include "control/MechanismDefs.h"
#include <QTimer>

/**
 * @brief 回转控制配置
 */
struct RotationConfig {
    MotorConfig motor;                  // 电机配置
    double defaultSpeed = 60.0;         // 默认转速(rpm)
    double maxTorque = 100.0;           // 最大力矩
    double minTorque = -100.0;          // 最小力矩

    // 关键位置 (A=不旋转, B=正向对接速度, C=逆向对接速度, D=程序调控速度)
    QMap<QString, double> keyPositions;

    static RotationConfig fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
};

/**
 * @brief 回转控制器 (Pr)
 *
 * 功能：
 * 1. 钻管旋转控制（速度模式）
 * 2. 转速控制
 * 3. 力矩模式（用于对接）
 *
 * 机构代号: Pr (Power Rotation)
 * 电机索引: 0
 */
class RotationController : public BaseMechanismController
{
    Q_OBJECT

public:
    // 机构代号
    static constexpr Mechanism::Code MechanismCode = Mechanism::Pr;

    explicit RotationController(IMotionDriver* driver,
                               const RotationConfig& config,
                               QObject* parent = nullptr);
    ~RotationController() override;

    // ========================================================================
    // BaseMechanismController接口实现
    // ========================================================================

    bool initialize() override;
    bool stop() override;
    bool reset() override;
    void updateStatus() override;

    // ========================================================================
    // 旋转控制
    // ========================================================================

    /**
     * @brief 开始旋转
     * @param rpm 转速（转/分钟），负值为反向
     * @return 成功返回true
     */
    bool startRotation(double rpm = -1);

    /**
     * @brief 停止旋转
     */
    bool stopRotation();

    /**
     * @brief 设置旋转速度
     * @param rpm 转速（转/分钟）
     */
    bool setSpeed(double rpm);

    /**
     * @brief 检查是否正在旋转
     */
    bool isRotating() const { return m_isRotating; }

    /**
     * @brief 获取当前设定转速
     */
    double speed() const { return m_speed; }

    /**
     * @brief 获取实际转速
     */
    double actualSpeed() const;

    // ========================================================================
    // 力矩模式
    // ========================================================================

    /**
     * @brief 设置力矩模式并施加力矩
     * @param dac 力矩值（DAC输出）
     */
    bool setTorque(double dac);

    /**
     * @brief 停止力矩输出
     */
    bool stopTorque();

    /**
     * @brief 是否处于力矩模式
     */
    bool isTorqueMode() const { return m_isTorqueMode; }

    // ========================================================================
    // 关键位置
    // ========================================================================

    /**
     * @brief 获取关键位置值（速度值）
     * @param key 位置代号 (A-D)
     * @return 速度值，不存在返回-1
     */
    double getKeyPosition(const QString& key) const;

    /**
     * @brief 应用关键位置的速度
     * @param key 位置代号 (A-D)
     * @return 成功返回true
     */
    bool applyKeySpeed(const QString& key);

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
    void updateConfig(const RotationConfig& config);

signals:
    /**
     * @brief 旋转状态变化信号
     */
    void rotationStateChanged(bool isRotating, double rpm);

    /**
     * @brief 速度变化信号
     */
    void speedChanged(double rpm);

private:
    RotationConfig m_config;

    bool m_isRotating;
    double m_speed;             // 设定转速(rpm)
    bool m_isTorqueMode;        // 是否处于力矩模式
};

#endif // ROTATIONCONTROLLER_H
