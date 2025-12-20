#ifndef MDBWORKER_H
#define MDBWORKER_H

#include "dataACQ/BaseWorker.h"
#include <QElapsedTimer>
#include <QModbusDataUnit>
#include <QModbusReply>
#include <QModbusTcpClient>
#include <QTimer>
#include <QTcpSocket>

/**
 * @brief MDB传感器采集Worker (Modbus TCP)
 * 
 * 硬件：Modbus TCP传感器组
 * 传感器类型：
 *   - 上拉力传感器 (Force_Upper)
 *   - 下拉力传感器 (Force_Lower)
 *   - 扭矩传感器 (Torque_MDB)
 *   - 位置传感器 (Position_MDB)
 * 默认采样频率：10Hz（可配置）
 * 数据格式：低频标量数据
 * 
 * 功能：
 * 1. 连接Modbus TCP服务器
 * 2. 定时读取传感器数据（10Hz）
 * 3. 处理零点校准
 * 4. 打包成DataBlock发送
 */
class MdbWorker : public BaseWorker
{
    Q_OBJECT
    
public:
    explicit MdbWorker(QObject *parent = nullptr);
    ~MdbWorker();
    
    // Modbus TCP配置
    void setServerAddress(const QString &address) { m_serverAddress = address; }
    void setServerPort(int port) { m_serverPort = port; }
    
    // 零点校准
    void setForceUpperZero(double zero) { m_forceUpperZero = zero; }
    void setForceLowerZero(double zero) { m_forceLowerZero = zero; }
    void setTorqueZero(double zero) { m_torqueZero = zero; }
    void setPositionZero(double zero) { m_positionZero = zero; }
    
    // 测试连接（不启动采集）
    Q_INVOKABLE bool testConnection();
    bool isConnected() const { return m_isConnected; }
    
    // 手动断开连接
    void disconnect();

public slots:
    void performZeroCalibration();  // 执行零点校准

protected:
    // 实现BaseWorker抽象方法
    bool initializeHardware() override;
    void shutdownHardware() override;
    void runAcquisition() override;

private slots:
    void readSensors();  // 定时读取传感器

private:
    bool connectToServer();
    void disconnectFromServer();
    bool readModbusRegister(int address, double &value);
    void sendDataBlock(SensorType type, double value);

private:
    QString m_serverAddress;    // Modbus TCP服务器地址
    int m_serverPort;           // 服务器端口
    
    QModbusTcpClient *m_modbusClient;  // Modbus TCP客户端
    QTimer *m_readTimer;               // 读取定时器
    
    // 零点校准值
    double m_forceUpperZero;
    double m_forceLowerZero;
    double m_torqueZero;
    double m_positionZero;
    
    // 最新读数（用于显示）
    double m_lastForceUpper;
    double m_lastForceLower;
    double m_lastTorque;
    double m_lastPosition;

    bool m_isConnected;
    qint64 m_sampleCount;       // 样本计数
    QElapsedTimer m_triggerTimer; // 统计定时器触发间隔
    qint64 m_lastIntervalMs {0};
};

#endif // MDBWORKER_H
