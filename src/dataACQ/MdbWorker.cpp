#include "dataACQ/MdbWorker.h"
#include "Logger.h"
#include "qeventloop.h"
#include <QDebug>
#include <QThread>

MdbWorker::MdbWorker(QObject *parent)
    : BaseWorker(parent)
    , m_serverAddress("192.168.1.200")
    , m_serverPort(502)
    , m_modbusClient(nullptr)
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
    m_sampleRate = 10.0;  // 默认10Hz
    LOG_DEBUG("MdbWorker", "Created. Default: 10Hz, 4 sensors");
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
    
    // 读取4个传感器数据
    double forceUpper, forceLower, torque, position;
    
    if (readModbusRegister(0x0000, forceUpper)) {
        m_lastForceUpper = forceUpper - m_forceUpperZero;
        sendDataBlock(SensorType::Force_Upper, m_lastForceUpper);
    }
    
    if (readModbusRegister(0x0001, forceLower)) {
        m_lastForceLower = forceLower - m_forceLowerZero;
        sendDataBlock(SensorType::Force_Lower, m_lastForceLower);
    }
    
    if (readModbusRegister(0x0002, torque)) {
        m_lastTorque = torque - m_torqueZero;
        sendDataBlock(SensorType::Torque_MDB, m_lastTorque);
    }
    
    if (readModbusRegister(0x0003, position)) {
        m_lastPosition = position - m_positionZero;
        sendDataBlock(SensorType::Position_MDB, m_lastPosition);
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

    // 读取当前值作为零点
    readModbusRegister(0x0000, m_forceUpperZero);
    readModbusRegister(0x0001, m_forceLowerZero);
    readModbusRegister(0x0002, m_torqueZero);
    readModbusRegister(0x0003, m_positionZero);

    LOG_DEBUG("MdbWorker", "Zero calibration done:");
    LOG_DEBUG_STREAM("MdbWorker") << "  Force Upper:" << m_forceUpperZero;
    LOG_DEBUG_STREAM("MdbWorker") << "  Force Lower:" << m_forceLowerZero;
    LOG_DEBUG_STREAM("MdbWorker") << "  Torque:" << m_torqueZero;
    LOG_DEBUG_STREAM("MdbWorker") << "  Position:" << m_positionZero;
}

bool MdbWorker::testConnection()
{
    LOG_DEBUG_STREAM("MdbWorker") << "Testing connection to" << m_serverAddress << ":" << m_serverPort;

    // 尝试连接
    if (!connectToServer()) {
        return false;
    }

    // 尝试读取一个寄存器来验证通信
    double testValue = 0.0;
    if (!readModbusRegister(0x0000, testValue)) {
        LOG_WARNING("MdbWorker", "Connection established but failed to read register");
        disconnectFromServer();
        emitError("Connected but cannot read data from Modbus server");
        return false;
    }

    LOG_DEBUG_STREAM("MdbWorker") << "Connection test successful, read value:" << testValue;
    return true;
}

void MdbWorker::disconnect()
{
    disconnectFromServer();
}

bool MdbWorker::connectToServer()
{
    LOG_DEBUG_STREAM("MdbWorker") << "Connecting to" << m_serverAddress << ":" << m_serverPort;

    // 在工作线程中创建 QModbusTcpClient（避免跨线程问题）
    if (!m_modbusClient) {
        LOG_DEBUG("MdbWorker", "Creating QModbusTcpClient in worker thread");
        m_modbusClient = new QModbusTcpClient(this);
    }

    // 如果已经连接，先断开
    if (m_modbusClient->state() == QModbusDevice::ConnectedState) {
        LOG_DEBUG("MdbWorker", "Already connected, disconnecting first...");
        m_modbusClient->disconnectDevice();
        QThread::msleep(100);
    }

    // 设置连接参数
    m_modbusClient->setConnectionParameter(QModbusDevice::NetworkPortParameter, m_serverPort);
    m_modbusClient->setConnectionParameter(QModbusDevice::NetworkAddressParameter, m_serverAddress);

    // 设置超时（5秒）
    m_modbusClient->setTimeout(5000);
    m_modbusClient->setNumberOfRetries(3);

    LOG_DEBUG("MdbWorker", "Attempting connection...");

    // 连接设备
    if (!m_modbusClient->connectDevice()) {
        QString errorMsg = QString("Failed to initiate connection: %1").arg(m_modbusClient->errorString());
        emitError(errorMsg);
        LOG_WARNING_STREAM("MdbWorker") << errorMsg;
        return false;
    }

    // 等待连接完成（最多5秒）
    if (m_modbusClient->state() == QModbusDevice::ConnectingState) {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);

        connect(m_modbusClient, &QModbusClient::stateChanged, &loop,
                [&loop, this]() {
                    if (m_modbusClient->state() != QModbusDevice::ConnectingState) {
                        loop.quit();
                    }
                });
        connect(m_modbusClient, &QModbusClient::errorOccurred, &loop, &QEventLoop::quit);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        timer.start(5000);
        loop.exec();
    }

    // 检查连接状态
    if (m_modbusClient->state() != QModbusDevice::ConnectedState) {
        QString errorMsg = QString("Connection failed: %1").arg(m_modbusClient->errorString());
        emitError(errorMsg);
        LOG_WARNING_STREAM("MdbWorker") << errorMsg;
        m_isConnected = false;
        return false;
    }

    m_isConnected = true;
    LOG_DEBUG_STREAM("MdbWorker") << "Successfully connected to" << m_serverAddress << ":" << m_serverPort;
    return true;
}

void MdbWorker::disconnectFromServer()
{
    if (!m_isConnected) {
        return;
    }

    LOG_DEBUG("MdbWorker", "Disconnecting...");

    if (m_modbusClient && m_modbusClient->state() == QModbusDevice::ConnectedState) {
        m_modbusClient->disconnectDevice();

        // 等待断开完成
        if (m_modbusClient->state() != QModbusDevice::UnconnectedState) {
            QEventLoop loop;
            QTimer timer;
            timer.setSingleShot(true);

            connect(m_modbusClient, &QModbusClient::stateChanged, &loop,
                    [&loop, this]() {
                        if (m_modbusClient->state() == QModbusDevice::UnconnectedState) {
                            loop.quit();
                        }
                    });
            connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

            timer.start(2000);
            loop.exec();
        }
    }

    m_isConnected = false;
    LOG_DEBUG("MdbWorker", "Disconnected");
}

bool MdbWorker::readModbusRegister(int address, double &value)
{
    if (!m_isConnected || !m_modbusClient) {
        LOG_DEBUG("MdbWorker", "readModbusRegister: Not connected");
        return false;
    }

    if (m_modbusClient->state() != QModbusDevice::ConnectedState) {
        LOG_DEBUG("MdbWorker", "readModbusRegister: Client not in connected state");
        return false;
    }

    LOG_DEBUG_STREAM("MdbWorker") << "Reading register" << address;

    // 读取2个寄存器（32位float）
    QModbusDataUnit readUnit(QModbusDataUnit::HoldingRegisters, address, 2);

    // 设备ID为1
    QModbusReply *reply = m_modbusClient->sendReadRequest(readUnit, 1);
    if (!reply) {
        LOG_WARNING_STREAM("MdbWorker") << "Failed to send read request for register" << address;
        return false;
    }

    LOG_DEBUG("MdbWorker", "Request sent, waiting for response...");

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
            LOG_WARNING("MdbWorker", "Timeout waiting for response");
            reply->deleteLater();
            return false;
        }
    }

    LOG_DEBUG("MdbWorker", "Response received");

    bool success = false;

    if (reply->error() == QModbusDevice::NoError) {
        QModbusDataUnit result = reply->result();
        LOG_DEBUG_STREAM("MdbWorker") << "Read successful, value count:" << result.valueCount();

        if (result.valueCount() >= 2) {
            // 模拟器使用大端序：高16位在前，低16位在后
            quint16 high = result.value(0);
            quint16 low = result.value(1);

            LOG_DEBUG_STREAM("MdbWorker") << "Raw values: high=" << Qt::hex << high << "low=" << low;

            // 组合成32位（大端序）
            quint32 rawValue = (static_cast<quint32>(high) << 16) | low;

            LOG_DEBUG_STREAM("MdbWorker") << "Combined raw value:" << Qt::hex << rawValue;

            // 将32位整数重新解释为float
            float floatValue;
            memcpy(&floatValue, &rawValue, sizeof(float));
            value = static_cast<double>(floatValue);

            LOG_DEBUG_STREAM("MdbWorker") << "Parsed float value:" << value;

            success = true;
        } else {
            LOG_WARNING_STREAM("MdbWorker") << "Insufficient values returned:" << result.valueCount();
        }
    } else {
        LOG_WARNING_STREAM("MdbWorker") << "Read error:" << reply->errorString();
        LOG_WARNING_STREAM("MdbWorker") << "Error code:" << reply->error();
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
