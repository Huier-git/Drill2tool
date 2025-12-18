#ifndef STORAGECONTROLLER_H
#define STORAGECONTROLLER_H

#include "control/BaseMechanismController.h"
#include "control/MechanismTypes.h"
#include "control/MechanismDefs.h"

/**
 * @brief 存储机构控制器 (Sr)
 *
 * 功能：
 * 1. 7位置转盘控制
 * 2. 位置索引管理
 * 3. 前进/后退转位
 * 4. 角度精确定位
 *
 * 机构代号: Sr (Storage Rotation)
 * 电机索引: 7
 */
class StorageController : public BaseMechanismController
{
    Q_OBJECT

public:
    static constexpr Mechanism::Code MechanismCode = Mechanism::Sr;

    explicit StorageController(IMotionDriver* driver,
                              const StorageConfig& config,
                              QObject* parent = nullptr);
    ~StorageController() override;
    
    bool initialize() override;
    bool stop() override;
    bool reset() override;
    void updateStatus() override;
    
    // 位置控制
    bool moveForward();                         // 前进一个位置
    bool moveBackward();                        // 后退一个位置
    bool moveToPosition(int position);          // 移动到指定位置(0-6)
    int currentPosition() const { return m_currentPosition; }
    
    // 零点管理
    bool resetZero();

    // ========================================================================
    // 关键位置
    // ========================================================================

    /**
     * @brief 获取关键位置脉冲值
     * @param key 位置代号 (A-G)
     * @return 脉冲值，不存在返回-1
     */
    double getKeyPosition(const QString& key) const;

    /**
     * @brief 移动到关键位置
     * @param key 位置代号 (A-G)
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
    void updateConfig(const StorageConfig& config);

signals:
    void positionChanged(int position);
    
private:
    double positionToAngle(int position) const;
    
    StorageConfig m_config;
    int m_currentPosition;                      // 当前位置(0-6)
    double m_angleOffset;                       // 零点偏移
};

#endif // STORAGECONTROLLER_H
