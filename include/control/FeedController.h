#ifndef FEEDCONTROLLER_H
#define FEEDCONTROLLER_H

#include "control/BaseMechanismController.h"
#include "control/MechanismTypes.h"
#include "control/MechanismDefs.h"
#include <QTimer>

/**
 * @brief 进给机构控制器 (Fz)
 *
 * 功能：
 * 1. 深度控制（基于毫米）
 * 2. 安全限位管理
 * 3. 脉冲-毫米转换
 * 4. 进给速度控制
 * 5. 自动进给到目标深度
 * 6. 紧急停止和安全位置
 *
 * 机构代号: Fz (Feed)
 * 电机索引: 2
 */
class FeedController : public BaseMechanismController
{
    Q_OBJECT

public:
    // 机构代号
    static constexpr Mechanism::Code MechanismCode = Mechanism::Fz;

    explicit FeedController(IMotionDriver* driver,
                           const PenetrationConfig& config,
                           QObject* parent = nullptr);
    ~FeedController() override;

    // ========================================================================
    // BaseMechanismController接口实现
    // ========================================================================

    bool initialize() override;
    bool stop() override;
    bool reset() override;
    void updateStatus() override;

    // ========================================================================
    // 深度控制
    // ========================================================================

    /**
     * @brief 设置目标深度并开始进给
     * @param depthMm 目标深度(mm)
     * @param speed 进给速度(可选，使用配置的默认速度)
     * @return 成功返回true
     */
    bool setTargetDepth(double depthMm, double speed = -1);

    /**
     * @brief 停止进给
     */
    bool stopFeed();

    /**
     * @brief 移动到安全位置（顶部）
     */
    bool gotoSafePosition();

    /**
     * @brief 移动到指定深度（绝对位置）
     */
    bool moveToDepth(double depthMm, double speed = -1);

    /**
     * @brief 向上移动指定距离
     */
    bool moveUp(double distanceMm);

    /**
     * @brief 向下移动指定距离
     */
    bool moveDown(double distanceMm);

    // ========================================================================
    // 深度查询
    // ========================================================================

    /**
     * @brief 获取当前深度
     * @return 当前深度(mm)
     */
    double currentDepth() const;

    /**
     * @brief 获取目标深度
     */
    double targetDepth() const { return m_targetDepth; }

    /**
     * @brief 获取当前脉冲位置
     */
    double currentPulse() const;

    /**
     * @brief 是否正在运动
     */
    bool isMoving() const { return m_isMoving; }

    // ========================================================================
    // 关键位置
    // ========================================================================

    /**
     * @brief 获取关键位置脉冲值
     * @param key 位置代号 (A-J)
     * @return 脉冲值，不存在返回-1
     */
    double getKeyPosition(const QString& key) const;

    /**
     * @brief 移动到关键位置
     * @param key 位置代号 (A-J)
     * @return 成功返回true
     */
    bool moveToKeyPosition(const QString& key);

    /**
     * @brief 获取所有关键位置名称
     */
    QStringList keyPositionNames() const;

    // ========================================================================
    // 限位管理
    // ========================================================================

    /**
     * @brief 设置深度限位
     * @param minMm 最小深度（底部，最大下钻深度）
     * @param maxMm 最大深度（顶部，安全位置）
     */
    bool setDepthLimits(double minMm, double maxMm);

    /**
     * @brief 获取深度限位
     */
    DepthLimits depthLimits() const { return m_config.depthLimits; }

    /**
     * @brief 设置零点偏移（校准）
     * @param offsetMm 偏移量(mm)
     */
    void setZeroOffset(double offsetMm);

    /**
     * @brief 获取零点偏移
     */
    double zeroOffset() const { return m_zeroOffsetMm; }

    // ========================================================================
    // 速度控制
    // ========================================================================

    /**
     * @brief 设置进给速度
     * @param speed 速度（脉冲/秒或mm/s，取决于配置）
     */
    bool setSpeed(double speed);

    /**
     * @brief 获取当前速度
     */
    double getSpeed() const { return m_currentSpeed; }

    // ========================================================================
    // 机构代号接口
    // ========================================================================

    /**
     * @brief 获取机构代号
     */
    Mechanism::Code mechanismCode() const { return MechanismCode; }

    /**
     * @brief 获取机构代号字符串
     */
    QString mechanismCodeString() const { return Mechanism::getCodeString(MechanismCode); }

    /**
     * @brief 更新配置（热更新）
     * @param config 新的配置
     */
    void updateConfig(const PenetrationConfig& config);

signals:
    /**
     * @brief 深度变化信号
     * @param depthMm 当前深度(mm)
     */
    void depthChanged(double depthMm);

    /**
     * @brief 到达目标深度信号
     */
    void targetReached();

    /**
     * @brief 安全限位触发信号
     * @param isMax true=触及最大深度限位, false=触及最小深度限位
     */
    void limitReached(bool isMax);

private:
    // 坐标转换
    double mmToPulses(double mm) const;
    double pulsesToMm(double pulses) const;

    // 检查安全性
    bool checkSafetyLimits(double depthMm) const;

    // 配置
    PenetrationConfig m_config;

    // 状态
    double m_targetDepth;       // 目标深度(mm)
    double m_currentSpeed;      // 当前速度
    double m_zeroOffsetMm;      // 零点偏移(mm)
    bool m_isMoving;            // 是否正在运动

    // 监控定时器
    QTimer* m_monitorTimer;
};

#endif // FEEDCONTROLLER_H
