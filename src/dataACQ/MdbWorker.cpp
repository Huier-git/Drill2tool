#include "dataACQ/MdbWorker.h"
#include "Logger.h"
#include "qeventloop.h"
#include <QDebug>
#include <QThread>

/*
 * Modbus TCP 设备映射说明（4个独立网关）：
 *
 * 设备索引 | IP地址          | 传感器类型     | 寄存器地址 | 设备ID | 数据解析
 * ---------|----------------|---------------|-----------|--------|----------
 * index 1  | 192.168.1.201  | 位置传感器     | 0x00      | 2      | 长字节拼接
 * index 2  | 192.168.1.202  | 扭矩传感器     | 0x00      | 1      | 长字节拼接
 * index 3  | 192.168.1.203  | 上/下压力传感器 | 450/452   | 1      | 补码转换
 *
 * 注意：设备索引从0开始，但实际设备编号从200开始（200+i）
 */

MdbWorker::MdbWorker(QObject *parent)
    : BaseWorker(parent)
    , m_serverAddress("192.168.1.200")
    , m_serverPort(502)
    , m_readTimer(nullptr)
    , m_forceUpperZero(0.0)
    , m_forceLowerZero(0.0)
    , m_torqueZero(0.0)
    , m_positionZero(0.0)
    , m_lastForceUpper(0.0)
    , m_lastForceLower(0.0)
    , m_lastTorque(0.0)
    , m_lastPosition(0.0)
    , m_isConnected(false)
    , m_sampleCount(0)
{
    // 初始化4个Modbus设备为nullptr
    for (int i = 0; i < 4; i++) {
        m_modbusDevices[i] = nullptr;
    }

    m_sampleRate = 10.0;  // 默认10Hz
    LOG_DEBUG("MdbWorker", "Created. Default: 10Hz, 4 sensors, 4 devices");
}

MdbWorker::~MdbWorker()
{
    if (m_isConnected) {
        disconnectFromServer();
    }
}

bool MdbWorker::initializeHardware()
{
    LOG_DEBUG("MdbWorker", "Initializing Modbus TCP connection...");
    LOG_DEBUG_STREAM("MdbWorker") << "  Server:" << m_serverAddress << ":" << m_serverPort;
    LOG_DEBUG_STREAM("MdbWorker") << "  Sample Rate:" << m_sampleRate << "Hz";

    // 连接到Modbus TCP服务器
    if (!connectToServer()) {
        return false;
    }

    // 创建定时器
    m_readTimer = new QTimer(this);
    int intervalMs = static_cast<int>(1000.0 / m_sampleRate);
    m_readTimer->setInterval(intervalMs);
    connect(m_readTimer, &QTimer::timeout, this, &MdbWorker::readSensors);

    LOG_DEBUG_STREAM("MdbWorker") << "Hardware initialized, read interval:" << intervalMs << "ms";
    return true;
}

void MdbWorker::shutdownHardware()
{
    LOG_DEBUG("MdbWorker", "Shutting down...");

    // 停止定时器
    if (m_readTimer) {
        m_readTimer->stop();
        delete m_readTimer;
        m_readTimer = nullptr;
    }

    // 断开连接
    disconnectFromServer();

    LOG_DEBUG_STREAM("MdbWorker") << "Shutdown complete. Total samples:" << m_sampleCount;
}

void MdbWorker::runAcquisition()
{
    LOG_DEBUG("MdbWorker", "Starting acquisition timer...");

    if (!m_readTimer) {
        emitError("Read timer not initialized");
        setState(WorkerState::Error);
        return;
    }

    // Start timer and return so the thread event loop can deliver timeouts.
    m_readTimer->start();
    LOG_DEBUG("MdbWorker", "Acquisition timer started");
}

void MdbWorker::readSensors()
{
    if (!shouldContinue()) {
        return;
    }

    // 根据Linux版本的设备映射读取传感器数据
    int successCount = 0;
    QVector<quint16> values;

    // 1. 读取上压力传感器 - 设备4 (index 3), 寄存器450, ID=1, 读取2个寄存器
    if (readFromDevice(3, 1, 450, 2, values) && values.size() >= 2) {
        // 使用补码转换 - 注意字节顺序：(values[1], values[0])
        int64_t rawValue = concatenateShortsToLong(values[1], values[0]);
        // 单位换算：原始值 * 0.00981 = N (牛顿)
        double forceUpper = static_cast<double>(rawValue) * 0.00981;
        m_lastForceUpper = forceUpper - m_forceUpperZero;
        sendDataBlock(SensorType::Force_Upper, m_lastForceUpper);
        successCount++;

        // 调试日志（仅第一次采样）
        if (m_sampleCount == 0) {
            LOG_DEBUG_STREAM("MdbWorker") << "First sample - Force Upper raw:" << rawValue << "converted:" << m_lastForceUpper << "N";
        }
    }

    // 2. 读取下压力传感器 - 设备4 (index 3), 寄存器452, ID=1, 读取2个寄存器
    if (readFromDevice(3, 1, 452, 2, values) && values.size() >= 2) {
        // 使用补码转换 - 注意字节顺序：(values[1], values[0])
        int64_t rawValue = concatenateShortsToLong(values[1], values[0]);
        // 单位换算：原始值 * 0.00981 = N (牛顿)
        double forceLower = static_cast<double>(rawValue) * 0.00981;
        m_lastForceLower = forceLower - m_forceLowerZero;
        sendDataBlock(SensorType::Force_Lower, m_lastForceLower);
        successCount++;
    }

    // 3. 读取扭矩传感器 - 设备3 (index 2), 寄存器0x00, ID=1, 读取2个寄存器
    if (readFromDevice(2, 1, 0x00, 2, values) && values.size() >= 2) {
        // 使用长字节拼接 - 注意字节顺序：(values[1], values[0])
        long rawValue = shortsToLong(values[1], values[0]);
        // 单位换算：原始值 * 0.01 = N·m
        double torque = static_cast<double>(rawValue) * 0.01;
        m_lastTorque = torque - m_torqueZero;
        sendDataBlock(SensorType::Torque_MDB, m_lastTorque);
        successCount++;
    }

    // 4. 读取位置传感器 - 设备2 (index 1), 寄存器0x00, ID=2, 读取2个寄存器
    if (readFromDevice(1, 2, 0x00, 2, values) && values.size() >= 2) {
        // 使用长字节拼接 - 注意字节顺序：(values[1], values[0])
        long rawValue = shortsToLong(values[1], values[0]);
        // 单位换算：负数修正后 * 150 / 4096 = mm
        double positionRaw;
        if (rawValue < 0) {
            positionRaw = 2 * 32767 + rawValue;  // 修正负数
        } else {
            positionRaw = static_cast<double>(rawValue);
        }
        double position = positionRaw * 150.0 / 4096.0;
        m_lastPosition = position - m_positionZero;
        sendDataBlock(SensorType::Position_MDB, m_lastPosition);
        successCount++;
    }

    // 掉线检测：如果4个传感器全部读取失败
    if (successCount == 0) {
        m_consecutiveFails++;
        if (m_consecutiveFails == 10 && !m_connectionLostReported) {
            // 连续10次全部失败，报告掉线
            emit eventOccurred("MdbSensorDisconnected",
                              "Modbus传感器连续10次读取失败，可能已掉线");
            m_connectionLostReported = true;
            LOG_WARNING("MdbWorker", "Modbus sensors appear disconnected (10 consecutive failures)");
        }
    } else {
        // 有成功读取，重置计数器
        if (m_consecutiveFails > 0) {
            if (m_connectionLostReported) {
                // 恢复连接
                emit eventOccurred("MdbSensorReconnected",
                                  QString("Modbus传感器恢复连接（失败计数: %1）").arg(m_consecutiveFails));
                LOG_DEBUG("MdbWorker", "Modbus sensors reconnected");
                m_connectionLostReported = false;
            }
            m_consecutiveFails = 0;
        }
    }

    m_sampleCount++;
    m_samplesCollected += 4;  // 4个传感器

    // Emit statistics roughly every 10 seconds at 10Hz.
    if (m_sampleCount > 0 && m_sampleCount % 100 == 0) {
        emit statisticsUpdated(m_sampleCount, m_sampleRate);
    }
}

void MdbWorker::performZeroCalibration()
{
    LOG_DEBUG("MdbWorker", "Performing zero calibration...");

    QVector<quint16> values;

    // 读取当前值作为零点（使用相同的换算公式和字节顺序）
    if (readFromDevice(3, 1, 450, 2, values) && values.size() >= 2) {
        int64_t rawValue = concatenateShortsToLong(values[1], values[0]);
        m_forceUpperZero = static_cast<double>(rawValue) * 0.00981;
    }

    if (readFromDevice(3, 1, 452, 2, values) && values.size() >= 2) {
        int64_t rawValue = concatenateShortsToLong(values[1], values[0]);
        m_forceLowerZero = static_cast<double>(rawValue) * 0.00981;
    }

    if (readFromDevice(2, 1, 0x00, 2, values) && values.size() >= 2) {
        long rawValue = shortsToLong(values[1], values[0]);
        m_torqueZero = static_cast<double>(rawValue) * 0.01;
    }

    if (readFromDevice(1, 2, 0x00, 2, values) && values.size() >= 2) {
        long rawValue = shortsToLong(values[1], values[0]);
        double positionRaw;
        if (rawValue < 0) {
            positionRaw = 2 * 32767 + rawValue;
        } else {
            positionRaw = static_cast<double>(rawValue);
        }
        m_positionZero = positionRaw * 150.0 / 4096.0;
    }

    LOG_DEBUG("MdbWorker", "Zero calibration done:");
    LOG_DEBUG_STREAM("MdbWorker") << "  Force Upper:" << m_forceUpperZero << "N";
    LOG_DEBUG_STREAM("MdbWorker") << "  Force Lower:" << m_forceLowerZero << "N";
    LOG_DEBUG_STREAM("MdbWorker") << "  Torque:" << m_torqueZero << "N·m";
    LOG_DEBUG_STREAM("MdbWorker") << "  Position:" << m_positionZero << "mm";
}

bool MdbWorker::testConnection()
{
    LOG_DEBUG_STREAM("MdbWorker") << "Testing connection to 4 Modbus devices from" << m_serverAddress << "...";

    // 尝试连接（已包含500ms稳定延迟）
    if (!connectToServer()) {
        return false;
    }

    LOG_DEBUG("MdbWorker", "Connection test successful");
    return true;
}

void MdbWorker::disconnect()
{
    disconnectFromServer();
}

bool MdbWorker::connectToServer()
{
    LOG_DEBUG_STREAM("MdbWorker") << "Connecting to 4 Modbus devices from" << m_serverAddress << "...";

    // 解析基础IP
    QStringList ipParts = m_serverAddress.split(".");
    if (ipParts.size() != 4) {
        LOG_WARNING("MdbWorker", "Invalid IP address format");
        return false;
    }

    int baseLastOctet = ipParts[3].toInt();

    // 连接4个设备
    for (int i = 0; i < 4; i++) {
        // 生成IP地址：192.168.1.200, 201, 202, 203
        QString deviceIp = QString("%1.%2.%3.%4")
            .arg(ipParts[0])
            .arg(ipParts[1])
            .arg(ipParts[2])
            .arg(baseLastOctet + i);

        LOG_DEBUG_STREAM("MdbWorker") << "Creating device" << i << "at" << deviceIp;

        // 创建设备
        if (!m_modbusDevices[i]) {
            m_modbusDevices[i] = new QModbusTcpClient(this);
        }

        // 如果已经连接，先断开
        if (m_modbusDevices[i]->state() == QModbusDevice::ConnectedState) {
            m_modbusDevices[i]->disconnectDevice();
            QThread::msleep(100);
        }

        // 设置连接参数
        m_modbusDevices[i]->setConnectionParameter(QModbusDevice::NetworkPortParameter, m_serverPort);
        m_modbusDevices[i]->setConnectionParameter(QModbusDevice::NetworkAddressParameter, deviceIp);
        m_modbusDevices[i]->setTimeout(5000);
        m_modbusDevices[i]->setNumberOfRetries(3);

        // 连接设备
        if (!m_modbusDevices[i]->connectDevice()) {
            LOG_WARNING_STREAM("MdbWorker") << "Failed to initiate connection to device" << i << ":" << m_modbusDevices[i]->errorString();
            continue;
        }

        // 等待连接完成（最多5秒）
        if (m_modbusDevices[i]->state() == QModbusDevice::ConnectingState) {
            QEventLoop loop;
            QTimer timer;
            timer.setSingleShot(true);

            connect(m_modbusDevices[i], &QModbusClient::stateChanged, &loop,
                    [&loop, this, i]() {
                        if (m_modbusDevices[i]->state() != QModbusDevice::ConnectingState) {
                            loop.quit();
                        }
                    });
            connect(m_modbusDevices[i], &QModbusClient::errorOccurred, &loop, &QEventLoop::quit);
            connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

            timer.start(5000);
            loop.exec();
        }

        // 检查连接状态
        if (m_modbusDevices[i]->state() == QModbusDevice::ConnectedState) {
            LOG_DEBUG_STREAM("MdbWorker") << "Device" << i << "connected successfully";
        } else {
            LOG_WARNING_STREAM("MdbWorker") << "Device" << i << "connection failed:" << m_modbusDevices[i]->errorString();
        }
    }

    // 只要有一个设备连接成功就算成功
    m_isConnected = false;
    for (int i = 0; i < 4; i++) {
        if (m_modbusDevices[i] && m_modbusDevices[i]->state() == QModbusDevice::ConnectedState) {
            m_isConnected = true;
            break;
        }
    }

    if (m_isConnected) {
        LOG_DEBUG("MdbWorker", "At least one device connected successfully");
        // 给设备一点时间稳定（参考Linux版本）
        QThread::msleep(500);
    } else {
        LOG_WARNING("MdbWorker", "All devices failed to connect");
    }

    return m_isConnected;
}

void MdbWorker::disconnectFromServer()
{
    if (!m_isConnected) {
        return;
    }

    LOG_DEBUG("MdbWorker", "Disconnecting all devices...");

    for (int i = 0; i < 4; i++) {
        if (m_modbusDevices[i] && m_modbusDevices[i]->state() == QModbusDevice::ConnectedState) {
            LOG_DEBUG_STREAM("MdbWorker") << "Disconnecting device" << i;
            m_modbusDevices[i]->disconnectDevice();

            // 等待断开完成
            if (m_modbusDevices[i]->state() != QModbusDevice::UnconnectedState) {
                QEventLoop loop;
                QTimer timer;
                timer.setSingleShot(true);

                connect(m_modbusDevices[i], &QModbusClient::stateChanged, &loop,
                        [&loop, this, i]() {
                            if (m_modbusDevices[i]->state() == QModbusDevice::UnconnectedState) {
                                loop.quit();
                            }
                        });
                connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

                timer.start(2000);
                loop.exec();
            }
        }
    }

    m_isConnected = false;
    LOG_DEBUG("MdbWorker", "All devices disconnected");
}

bool MdbWorker::readFromDevice(int deviceIndex, int deviceId, int registerAddr, int numRegisters, QVector<quint16> &values)
{
    if (deviceIndex < 0 || deviceIndex >= 4) {
        LOG_WARNING_STREAM("MdbWorker") << "Invalid device index:" << deviceIndex;
        return false;
    }

    if (!m_modbusDevices[deviceIndex]) {
        LOG_DEBUG_STREAM("MdbWorker") << "Device" << deviceIndex << "not created";
        return false;
    }

    if (m_modbusDevices[deviceIndex]->state() != QModbusDevice::ConnectedState) {
        LOG_DEBUG_STREAM("MdbWorker") << "Device" << deviceIndex << "not connected";
        return false;
    }

    // 读取寄存器
    QModbusDataUnit readUnit(QModbusDataUnit::HoldingRegisters, registerAddr, numRegisters);
    QModbusReply *reply = m_modbusDevices[deviceIndex]->sendReadRequest(readUnit, deviceId);
    if (!reply) {
        LOG_WARNING_STREAM("MdbWorker") << "Failed to send read request to device" << deviceIndex;
        return false;
    }

    // 等待响应（最多2000ms）
    if (!reply->isFinished()) {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);

        connect(reply, &QModbusReply::finished, &loop, &QEventLoop::quit);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        timer.start(2000);
        loop.exec();

        if (!reply->isFinished()) {
            LOG_WARNING_STREAM("MdbWorker") << "Timeout reading from device" << deviceIndex;
            reply->deleteLater();
            return false;
        }
    }

    bool success = false;
    if (reply->error() == QModbusDevice::NoError) {
        QModbusDataUnit result = reply->result();
        if (static_cast<int>(result.valueCount()) >= numRegisters) {
            values.clear();
            for (int i = 0; i < numRegisters; i++) {
                values.append(result.value(i));
            }
            success = true;
        }
    } else {
        LOG_WARNING_STREAM("MdbWorker") << "Read error from device" << deviceIndex << ":" << reply->errorString();
    }

    reply->deleteLater();
    return success;
}

void MdbWorker::sendDataBlock(SensorType type, double value)
{
    DataBlock block;
    block.roundId = m_currentRoundId;
    block.sensorType = type;
    block.channelId = 0;  // MDB传感器不分通道
    block.startTimestampUs = currentTimestampUs();
    block.sampleRate = m_sampleRate;
    block.numSamples = 1;
    block.values.append(value);

    emit dataBlockReady(block);
}

// 数据解析辅助函数
int64_t MdbWorker::concatenateShortsToLong(int16_t lower, int16_t upper)
{
    // 将2个短字节拼成一个长字节，并转换标准补码成10进制（用于拉力传感器）
    int64_t result = ((static_cast<int64_t>(upper) << 16) & 0xFFFF0000) | (lower & 0x0000FFFF);

    // 检查是否为负数（补码）
    if (result & 0x80000000) {
        // 如果是负数，执行补码转换
        result = -((~result + 1) & 0xFFFFFFFF);
    }

    return result;
}

long MdbWorker::shortsToLong(int16_t short1, int16_t short2)
{
    // 将两个短字节拼成一个长字节（用于扭矩、位置传感器）
    long long_byte = (short2 << 16) | short1;
    return long_byte;
}
