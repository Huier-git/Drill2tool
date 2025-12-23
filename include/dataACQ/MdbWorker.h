#ifndef MDBWORKER_H
#define MDBWORKER_H

#include "dataACQ/BaseWorker.h"
#include <QTimer>
#include <QTcpSocket>
#include <QModbusTcpClient>
#include <QModbusDataUnit>
#include <QModbusReply>

/**
 * @brief MDB传感器采集Worker (Modbus TCP)
 *
 * 硬件：4个独立的Modbus TCP设备
 * 设备映射：
 *   - 设备1 (192.168.1.200): 保留
 *   - 设备2 (192.168.1.201): 位置传感器，寄存器0x00，设备ID=2
 *   - 设备3 (192.168.1.202): 扭矩传感器，寄存器0x00，设备ID=1
 *   - 设备4 (192.168.1.203): 上下压力传感器，寄存器450/452，设备ID=1
 * 传感器类型：
 *   - 上拉力传感器 (Force_Upper)
 *   - 下拉力传感器 (Force_Lower)
 *   - 扭矩传感器 (Torque_MDB)
 *   - 位置传感器 (Position_MDB)
 * 默认采样频率：10Hz（可配置）
 * 数据格式：低频标量数据
 *
 * 功能：
 * 1. 连接4个独立的Modbus TCP设备
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
    bool readFromDevice(int deviceIndex, int deviceId, int registerAddr, int numRegisters, QVector<quint16> &values);
    void sendDataBlock(SensorType type, double value);

    // 数据解析辅助函数
    int64_t concatenateShortsToLong(int16_t lower, int16_t upper);
    long shortsToLong(int16_t short1, int16_t short2);

private:
    QString m_serverAddress;    // Modbus TCP基础地址 (192.168.1.200)
    int m_serverPort;           // 服务器端口

    QModbusTcpClient *m_modbusDevices[4];  // 4个Modbus TCP设备
    QTimer *m_readTimer;                    // 读取定时器
    
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
};

#endif // MDBWORKER_H
