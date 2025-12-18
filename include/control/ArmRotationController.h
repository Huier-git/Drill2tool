#ifndef ARMROTATIONCONTROLLER_H
#define ARMROTATIONCONTROLLER_H

#include "control/BaseMechanismController.h"
#include "control/MechanismTypes.h"
#include "control/MechanismDefs.h"

/**
 * @brief 机械手回转位置枚举
 */
enum class ArmPosition {
    Unknown,
    Drill,      // 钻机位置
    Storage     // 料仓位置
};

/**
 * @brief 机械手回转配置
 */
struct ArmRotationConfig {
    MotorConfig motor;
    double drillPositionAngle = 0.0;        // 钻机位置角度
    double storagePositionAngle = 180.0;    // 料仓位置角度
    double pulsesPerDegree = 1000.0;        // 每度脉冲数
    double positionTolerance = 0.5;         // 位置容差(度)

    // 关键位置 (A=对准存储机构, B=对准对接头)
    QMap<QString, double> keyPositions;

    static ArmRotationConfig fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
};

/**
 * @brief 机械手回转控制器 (Mr)
 *
 * 功能：
 * 1. 角度位置控制
 * 2. 预设位置(钻机/料仓)快速切换
 * 3. 零点重置
 *
 * 机构代号: Mr (Manipulator Rotation)
 * 电机索引: 5
 */
class ArmRotationController : public BaseMechanismController
{
    Q_OBJECT

public:
    static constexpr Mechanism::Code MechanismCode = Mechanism::Mr;

    explicit ArmRotationController(IMotionDriver* driver,
                                  const ArmRotationConfig& config,
                                  QObject* parent = nullptr);
    ~ArmRotationController() override;

    // BaseMechanismController接口
    bool initialize() override;
    bool stop() override;
    bool reset() override;
    void updateStatus() override;

    // ========================================================================
    // 回转控制
    // ========================================================================

    /**
     * @brief 设置回转角度
     * @param angle 目标角度(度)
     */
    bool setAngle(double angle);

    /**
     * @brief 旋转到预设位置
     * @param position 目标位置(Drill/Storage)
     */
    bool rotateToPosition(ArmPosition position);

    /**
     * @brief 旋转到钻机位置
     */
    bool rotateToDrill();

    /**
     * @brief 旋转到料仓位置
     */
    bool rotateToStorage();

    /**
     * @brief 获取当前角度
     */
    double currentAngle() const;

    /**
     * @brief 获取当前位置
     */
    ArmPosition currentPosition() const;

    /**
     * @brief 是否正在旋转
     */
    bool isRotating() const { return m_isRotating; }

    // ========================================================================
    // 零点管理
    // ========================================================================

    /**
     * @brief 重置零点
     */
    bool resetZero();

    /**
     * @brief 设置角度偏移
     */
    void setOffset(double offset) { m_offset = offset; }

    /**
     * @brief 获取角度偏移
     */
    double offset() const { return m_offset; }

    // ========================================================================
    // 关键位置
    // ========================================================================

    /**
     * @brief 获取关键位置角度值
     * @param key 位置代号 (A-B)
     * @return 角度值，不存在返回-1
     */
    double getKeyPosition(const QString& key) const;

    /**
     * @brief 移动到关键位置
     * @param key 位置代号 (A-B)
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
    void updateConfig(const ArmRotationConfig& config);

signals:
    void angleChanged(double angle);
    void positionReached(ArmPosition position);
    void rotationCompleted();

private:
    ArmRotationConfig m_config;

    double m_offset;            // 角度偏移
    bool m_isRotating;
    ArmPosition m_currentPosition;

    // 辅助函数
    double angleToPulses(double angle) const;
    double pulsesToAngle(double pulses) const;
    ArmPosition determinePosition(double angle) const;
};

#endif // ARMROTATIONCONTROLLER_H
