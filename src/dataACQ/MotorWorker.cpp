#include "dataACQ/MotorWorker.h"
#include <QDebug>
#include <QDateTime>
#include <QThread>

MotorWorker::MotorWorker(QObject *parent)
    : BaseWorker(parent)
    , m_controllerAddress("192.168.1.11")
    , m_tcpSocket(nullptr)
    , m_readTimer(nullptr)
    , m_readPosition(true)
    , m_readSpeed(true)
    , m_readTorque(true)
    , m_readCurrent(true)
    , m_isConnected(false)
    , m_sampleCount(0)
{
    m_sampleRate = 100.0;  // 默认100Hz
    m_motorIds = {0, 1, 2, 3};  // 默认4个电机
    qDebug() << "[MotorWorker] Created. Default: 100Hz, 4 motors, address: 192.168.1.11";
}

MotorWorker::~MotorWorker()
{
    if (m_isConnected) {
        disconnectFromController();
    }
}

void MotorWorker::setReadParameters(bool pos, bool speed, bool torque, bool current)
{
    m_readPosition = pos;
    m_readSpeed = speed;
    m_readTorque = torque;
    m_readCurrent = current;
    
    qDebug() << "[MotorWorker] Read parameters set:"
             << "Pos=" << pos << "Speed=" << speed 
             << "Torque=" << torque << "Current=" << current;
}

bool MotorWorker::initializeHardware()
{
    qDebug() << "[MotorWorker] Initializing ZMotion controller...";
    qDebug() << "  Controller:" << m_controllerAddress;
    qDebug() << "  Sample Rate:" << m_sampleRate << "Hz";
    qDebug() << "  Motor IDs:" << m_motorIds;
    
    // 连接到ZMotion控制器
    if (!connectToController()) {
        return false;
    }
    
    // 创建定时器
    m_readTimer = new QTimer(this);
    int intervalMs = static_cast<int>(1000.0 / m_sampleRate);
    m_readTimer->setInterval(intervalMs);
    connect(m_readTimer, &QTimer::timeout, this, &MotorWorker::readMotorParameters);
    
    qDebug() << "[MotorWorker] Hardware initialized, read interval:" << intervalMs << "ms";
    return true;
}

void MotorWorker::shutdownHardware()
{
    qDebug() << "[MotorWorker] Shutting down...";
    
    // 停止定时器
    if (m_readTimer) {
        m_readTimer->stop();
        delete m_readTimer;
        m_readTimer = nullptr;
    }
    
    // 断开连接
    disconnectFromController();
    
    qDebug() << "[MotorWorker] Shutdown complete. Total samples:" << m_sampleCount;
}

void MotorWorker::runAcquisition()
{
    qDebug() << "[MotorWorker] Starting acquisition timer...";
    
    // 启动定时器
    m_readTimer->start();
    
    // 进入事件循环
    while (shouldContinue()) {
        QThread::msleep(100);
        
        // 每10秒输出统计
        if (m_sampleCount > 0 && m_sampleCount % 1000 == 0) {
            emit statisticsUpdated(m_samplesCollected, m_sampleRate);
        }
    }
    
    qDebug() << "[MotorWorker] Acquisition loop ended";
}

void MotorWorker::readMotorParameters()
{
    if (!shouldContinue()) {
        return;
    }
    
    // 遍历所有电机
    for (int motorId : m_motorIds) {
        // 读取位置
        if (m_readPosition) {
            double position;
            if (readMotorPosition(motorId, position)) {
                sendDataBlock(motorId, SensorType::Motor_Position, position);
            }
        }
        
        // 读取速度
        if (m_readSpeed) {
            double speed;
            if (readMotorSpeed(motorId, speed)) {
                sendDataBlock(motorId, SensorType::Motor_Speed, speed);
            }
        }
        
        // 读取扭矩
        if (m_readTorque) {
            double torque;
            if (readMotorTorque(motorId, torque)) {
                sendDataBlock(motorId, SensorType::Motor_Torque, torque);
            }
        }
        
        // 读取电流
        if (m_readCurrent) {
            double current;
            if (readMotorCurrent(motorId, current)) {
                sendDataBlock(motorId, SensorType::Motor_Current, current);
            }
        }
    }
    
    m_sampleCount++;
    int paramsPerMotor = (m_readPosition ? 1 : 0) + (m_readSpeed ? 1 : 0) + 
                         (m_readTorque ? 1 : 0) + (m_readCurrent ? 1 : 0);
    m_samplesCollected += m_motorIds.size() * paramsPerMotor;
}

bool MotorWorker::connectToController()
{
    qDebug() << "[MotorWorker] Connecting to ZMotion at" << m_controllerAddress << ":8001";
    
    // 创建 TCP socket
    if (m_tcpSocket) {
        delete m_tcpSocket;
    }
    m_tcpSocket = new QTcpSocket(this);
    
    // 连接到 ZMotion 服务器（模拟器使用端口 8001）
    m_tcpSocket->connectToHost(m_controllerAddress, 8001);
    
    // 等待连接（最多5秒）
    if (!m_tcpSocket->waitForConnected(5000)) {
        QString errorMsg = QString("Failed to connect to ZMotion: %1").arg(m_tcpSocket->errorString());
        emitError(errorMsg);
        qWarning() << "[MotorWorker]" << errorMsg;
        delete m_tcpSocket;
        m_tcpSocket = nullptr;
        m_isConnected = false;
        return false;
    }
    
    m_isConnected = true;
    qDebug() << "[MotorWorker] Successfully connected to ZMotion";
    return true;
}

void MotorWorker::disconnectFromController()
{
    if (!m_isConnected) {
        return;
    }
    
    qDebug() << "[MotorWorker] Disconnecting from ZMotion...";
    
    if (m_tcpSocket) {
        m_tcpSocket->disconnectFromHost();
        if (m_tcpSocket->state() != QAbstractSocket::UnconnectedState) {
            m_tcpSocket->waitForDisconnected(1000);
        }
        delete m_tcpSocket;
        m_tcpSocket = nullptr;
    }
    
    m_isConnected = false;
    qDebug() << "[MotorWorker] Disconnected";
}

bool MotorWorker::testConnection()
{
    qDebug() << "[MotorWorker] Testing connection to ZMotion...";
    
    // 尝试连接
    if (!connectToController()) {
        return false;
    }
    
    // 测试读取第一个电机的位置
    if (m_tcpSocket && m_motorIds.size() > 0) {
        QString command = QString("GET_DPOS(%1)\n").arg(m_motorIds[0]);
        m_tcpSocket->write(command.toUtf8());
        m_tcpSocket->flush();
        
        if (m_tcpSocket->waitForReadyRead(1000)) {
            QByteArray response = m_tcpSocket->readAll();
            QString responseStr = QString::fromUtf8(response).trimmed();
            
            if (responseStr.startsWith("OK:")) {
                qDebug() << "[MotorWorker] Connection test successful, response:" << responseStr;
                return true;
            } else {
                qWarning() << "[MotorWorker] Unexpected response:" << responseStr;
                disconnectFromController();
                emitError("Connected but received unexpected response from ZMotion");
                return false;
            }
        } else {
            qWarning() << "[MotorWorker] No response from ZMotion";
            disconnectFromController();
            emitError("Connected but ZMotion not responding");
            return false;
        }
    }
    
    return true;
}

void MotorWorker::disconnect()
{
    disconnectFromController();
}

bool MotorWorker::readMotorPosition(int motorId, double &position)
{
    // TODO: 读取电机位置
    /*
    if (!m_isConnected || !m_zmotionHandle) {
        return false;
    }
    
    float pos;
    int result = ZAux_Direct_GetDpos(m_zmotionHandle, motorId, &pos);
    if (result != 0) {
        return false;
    }
    
    position = static_cast<double>(pos);
    return true;
    */
    
    // 模拟数据
    position = 1000.0 + motorId * 100.0 + (qrand() % 100) / 10.0;
    return true;
}

bool MotorWorker::readMotorSpeed(int motorId, double &speed)
{
    // TODO: 读取电机速度
    /*
    if (!m_isConnected || !m_zmotionHandle) {
        return false;
    }
    
    float spd;
    int result = ZAux_Direct_GetSpeed(m_zmotionHandle, motorId, &spd);
    if (result != 0) {
        return false;
    }
    
    speed = static_cast<double>(spd);
    return true;
    */
    
    // 模拟数据
    speed = 50.0 + motorId * 10.0 + (qrand() % 50) / 10.0;
    return true;
}

bool MotorWorker::readMotorTorque(int motorId, double &torque)
{
    // TODO: 读取电机扭矩（如果ZMotion支持）
    /*
    if (!m_isConnected || !m_zmotionHandle) {
        return false;
    }
    
    float trq;
    int result = ZAux_Direct_GetAIn(m_zmotionHandle, motorId, &trq);
    if (result != 0) {
        return false;
    }
    
    torque = static_cast<double>(trq);
    return true;
    */
    
    // 模拟数据
    torque = 20.0 + motorId * 5.0 + (qrand() % 20) / 10.0;
    return true;
}

bool MotorWorker::readMotorCurrent(int motorId, double &current)
{
    // TODO: 读取电机电流
    /*
    if (!m_isConnected || !m_zmotionHandle) {
        return false;
    }
    
    float cur;
    int result = ZAux_Direct_GetAxisCur(m_zmotionHandle, motorId, &cur);
    if (result != 0) {
        return false;
    }
    
    current = static_cast<double>(cur);
    return true;
    */
    
    // 模拟数据
    current = 2.0 + motorId * 0.5 + (qrand() % 10) / 10.0;
    return true;
}

void MotorWorker::sendDataBlock(int motorId, SensorType type, double value)
{
    DataBlock block;
    block.roundId = m_currentRoundId;
    block.sensorType = type;
    block.channelId = motorId;  // 通道ID = 电机ID
    block.startTimestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
    block.sampleRate = m_sampleRate;
    block.numSamples = 1;
    block.values.append(value);
    
    emit dataBlockReady(block);
}
