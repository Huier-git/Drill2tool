#ifndef CLAMPCONTROLLER_H
#define CLAMPCONTROLLER_H

#include "control/BaseMechanismController.h"
#include "control/MechanismTypes.h"
#include "control/MechanismDefs.h"
#include <QTimer>

/**
 * @brief 夹紧机构控制器（下夹紧）(Cb)
 *
 * 功能：
 * 1. 夹紧/松开控制（力矩模式）
 * 2. 力矩可调
 * 3. 初始化找零点
 * 4. 位置锁定
 *
 * 机构代号: Cb (Clamp Base)
 * 电机索引: 3
 */
class ClampController : public BaseMechanismController
{
    Q_OBJECT

public:
    static constexpr Mechanism::Code MechanismCode = Mechanism::Cb;

    explicit ClampController(IMotionDriver* driver,
                            const ClampConfig& config,
                            QObject* parent = nullptr);
    ~ClampController() override;
    
    bool initialize() override;
    bool stop() override;
    bool reset() override;
    void updateStatus() override;
    
    // 夹紧控制
    bool open();
    bool close(double torque = -1);
    bool initializeClamp();
    
    ClampState state() const { return m_state; }
    void setTorque(double torque) { m_torque = torque; }
    double torque() const { return m_torque; }

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
    void updateConfig(const ClampConfig& config);

signals:
    void stateChanged(ClampState state);
    
private slots:
    void monitorInit();
    
private:
    ClampConfig m_config;
    ClampState m_state;
    double m_torque;
    
    bool m_isInitializing;
    double m_lastPosition;
    int m_stableCount;
    QTimer* m_initTimer;
};

#endif // CLAMPCONTROLLER_H
