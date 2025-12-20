#ifndef MOTORWORKER_H
#define MOTORWORKER_H

#include "dataACQ/BaseWorker.h"
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>

/**
 * @brief ZMotion电机参数采集Worker
 *
 * 硬件：ZMotion运动控制器
 * 传感器类型：
 *   - 电机位置 (Motor_Position)
 *   - 电机速度 (Motor_Speed)
 *   - 电机扭矩 (Motor_Torque)
 *   - 电机电流 (Motor_Current)
 * 默认采样频率：100Hz（可配置）
 * 数据格式：低频标量数据
 *
 * 功能：
 * 1. 使用全局 g_handle 读取电机参数（只读，不负责连接）
 * 2. 定时读取电机参数
 * 3. 支持多电机同时采集
 * 4. 打包成DataBlock发送
 *
 * 注意：
 * - 此类只负责数据采集（只读），不负责连接管理
 * - 连接由 ZMotionDriver 统一管理
 * - 使用全局 g_handle 和 g_mutex
 */
class MotorWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit MotorWorker(QObject *parent = nullptr);
    ~MotorWorker();

    // ZMotion配置
    void setControllerAddress(const QString &address) { m_controllerAddress = address; }
    void setMotorIds(const QVector<int> &ids) { m_motorIds = ids; }
    void setReadParameters(bool pos, bool speed, bool torque, bool current);

    // 检查全局句柄是否已连接
    bool isConnected() const;

public slots:
    void readMotorParameters();  // 读取电机参数

signals:
    // 连接状态改变（由外部连接管理器触发）
    void connectionStateChanged(bool connected);

protected:
    // 实现BaseWorker抽象方法
    bool initializeHardware() override;
    void shutdownHardware() override;
    void runAcquisition() override;

private:
    bool readMotorPosition(int motorId, double &position);
    bool readMotorSpeed(int motorId, double &speed);
    bool readMotorTorque(int motorId, double &torque);
    bool readMotorCurrent(int motorId, double &current);
    void sendDataBlock(int motorId, SensorType type, double value);

private:
    QString m_controllerAddress;    // 控制器地址（仅用于日志）
    QVector<int> m_motorIds;        // 电机ID列表
    QTimer *m_readTimer;            // 读取定时器

    // 读取参数开关
    bool m_readPosition;
    bool m_readSpeed;
    bool m_readTorque;
    bool m_readCurrent;

    qint64 m_sampleCount;           // 样本计数
    QElapsedTimer m_triggerTimer;   // 统计定时器触发间隔
    qint64 m_lastIntervalMs {0};
};

#endif // MOTORWORKER_H
