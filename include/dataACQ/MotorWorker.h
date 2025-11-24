#ifndef MOTORWORKER_H
#define MOTORWORKER_H

#include "dataACQ/BaseWorker.h"
#include <QTimer>
#include <QVector>
#include <QTcpSocket>

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
 * 1. 连接ZMotion控制器
 * 2. 定时读取电机参数
 * 3. 支持多电机同时采集
 * 4. 打包成DataBlock发送
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
    
    // 测试连接
    Q_INVOKABLE bool testConnection();
    bool isConnected() const { return m_isConnected; }
    void disconnect();

public slots:
    void readMotorParameters();  // 读取电机参数

protected:
    // 实现BaseWorker抽象方法
    bool initializeHardware() override;
    void shutdownHardware() override;
    void runAcquisition() override;

private:
    bool connectToController();
    void disconnectFromController();
    bool readMotorPosition(int motorId, double &position);
    bool readMotorSpeed(int motorId, double &speed);
    bool readMotorTorque(int motorId, double &torque);
    bool readMotorCurrent(int motorId, double &current);
    void sendDataBlock(int motorId, SensorType type, double value);

private:
    QString m_controllerAddress;    // 控制器地址
    QVector<int> m_motorIds;        // 电机ID列表
    QTcpSocket *m_tcpSocket;        // TCP连接（用于模拟器）
    QTimer *m_readTimer;            // 读取定时器
    
    // 读取参数开关
    bool m_readPosition;
    bool m_readSpeed;
    bool m_readTorque;
    bool m_readCurrent;
    
    bool m_isConnected;
    qint64 m_sampleCount;           // 样本计数
    
    // ZMotion句柄（实际实现时使用）
    // void* m_zmotionHandle;
};

#endif // MOTORWORKER_H
