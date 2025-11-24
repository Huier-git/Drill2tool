#include "dataACQ/VibrationWorker.h"
// #include "VK70xNMC_DAQ2.h"  // VK701硬件库头文件（暂时注释）
#include <QDebug>
#include <QDateTime>
#include <QThread>

VibrationWorker::VibrationWorker(QObject *parent)
    : BaseWorker(parent)
    , m_cardId(0)
    , m_port(8234)
    , m_serverAddress("127.0.0.1")
    , m_channelCount(3)
    , m_blockSize(1000)
    , m_tcpSocket(nullptr)
    , m_baseTimestamp(0)
    , m_blockSequence(0)
    , m_isCardConnected(false)
    , m_isSampling(false)
{
    m_sampleRate = 5000.0;  // 默认5000Hz
    qDebug() << "[VibrationWorker] Created. Default: 5000Hz, 3 channels, port 8234";
}

VibrationWorker::~VibrationWorker()
{
    if (m_isSampling) {
        stopSampling();
    }
}

bool VibrationWorker::initializeHardware()
{
    qDebug() << "[VibrationWorker] Initializing VK701 hardware...";
    qDebug() << "  Card ID:" << m_cardId;
    qDebug() << "  Port:" << m_port;
    qDebug() << "  Sample Rate:" << m_sampleRate << "Hz";
    qDebug() << "  Channels:" << m_channelCount;
    
    // 连接采集卡
    if (!connectToCard()) {
        return false;
    }
    
    // 配置通道
    if (!configureChannels()) {
        return false;
    }
    
    // 启动采样
    if (!startSampling()) {
        return false;
    }
    
    // 记录基准时间戳
    m_baseTimestamp = QDateTime::currentMSecsSinceEpoch() * 1000;
    m_timer.start();
    m_blockSequence = 0;
    
    qDebug() << "[VibrationWorker] Hardware initialized successfully";
    return true;
}

void VibrationWorker::shutdownHardware()
{
    qDebug() << "[VibrationWorker] Shutting down...";
    
    stopSampling();
    
    // TODO: 关闭VK701连接
    // Server_TCPClose(m_port);
    
    m_isCardConnected = false;
    m_isSampling = false;
    
    qDebug() << "[VibrationWorker] Shutdown complete";
}

void VibrationWorker::runAcquisition()
{
    qDebug() << "[VibrationWorker] Acquisition loop started";
    
    while (shouldContinue()) {
        // 读取一个数据块
        if (!readDataBlock()) {
            emitError("Failed to read data block from VK701");
            QThread::msleep(10);  // 出错后短暂延迟
            continue;
        }
        
        // 更新统计信息（每100个块更新一次）
        if (m_blockSequence % 100 == 0) {
            emit statisticsUpdated(m_samplesCollected, m_sampleRate);
        }
    }
    
    qDebug() << "[VibrationWorker] Acquisition loop ended";
}

bool VibrationWorker::connectToCard()
{
    qDebug() << "[VibrationWorker] Connecting to VK701 TCP server at" << m_serverAddress << ":" << m_port;
    
    // 创建 TCP socket
    if (m_tcpSocket) {
        delete m_tcpSocket;
    }
    m_tcpSocket = new QTcpSocket(this);
    
    // 连接到服务器
    m_tcpSocket->connectToHost(m_serverAddress, m_port);
    
    // 等待连接（最多5秒）
    if (!m_tcpSocket->waitForConnected(5000)) {
        QString errorMsg = QString("Failed to connect to VK701 server: %1").arg(m_tcpSocket->errorString());
        emitError(errorMsg);
        qWarning() << "[VibrationWorker]" << errorMsg;
        delete m_tcpSocket;
        m_tcpSocket = nullptr;
        m_isCardConnected = false;
        return false;
    }
    
    m_isCardConnected = true;
    qDebug() << "[VibrationWorker] Successfully connected to VK701 server";
    return true;
}

void VibrationWorker::disconnectFromCard()
{
    if (!m_isCardConnected) {
        return;
    }
    
    qDebug() << "[VibrationWorker] Disconnecting from VK701...";
    
    if (m_tcpSocket) {
        m_tcpSocket->disconnectFromHost();
        if (m_tcpSocket->state() != QAbstractSocket::UnconnectedState) {
            m_tcpSocket->waitForDisconnected(1000);
        }
        delete m_tcpSocket;
        m_tcpSocket = nullptr;
    }
    
    m_isCardConnected = false;
    qDebug() << "[VibrationWorker] Disconnected";
}

bool VibrationWorker::testConnection()
{
    qDebug() << "[VibrationWorker] Testing connection to VK701...";
    
    // 尝试连接
    if (!connectToCard()) {
        return false;
    }
    
    // 简单测试：等待少量数据
    if (m_tcpSocket && m_tcpSocket->waitForReadyRead(1000)) {
        qDebug() << "[VibrationWorker] Connection test successful, received data";
        return true;
    } else {
        qWarning() << "[VibrationWorker] Connection established but no data received";
        // 不断开连接，因为数据可能稍后才来
        return true;
    }
}

void VibrationWorker::disconnect()
{
    disconnectFromCard();
}

bool VibrationWorker::configureChannels()
{
    qDebug() << "[VibrationWorker] Configuring channels...";
    
    // TODO: 配置VK701通道参数
    /*
    int result;
    
    // 设置采样频率
    result = VK70xNMC_SetSampleFreq(m_cardId, m_sampleRate);
    if (result != 0) {
        emitError(QString("Failed to set sample frequency: %1").arg(result));
        return false;
    }
    
    // 配置每个通道
    for (int ch = 0; ch < m_channelCount; ++ch) {
        result = VK70xNMC_SetChannelMode(m_cardId, ch, 1);  // 启用通道
        if (result != 0) {
            emitError(QString("Failed to enable channel %1: %2").arg(ch).arg(result));
            return false;
        }
    }
    */
    
    qDebug() << "[VibrationWorker] Channels configured (SIMULATED)";
    return true;
}

bool VibrationWorker::startSampling()
{
    qDebug() << "[VibrationWorker] Starting sampling...";
    
    // TODO: 启动VK701采样
    /*
    int result = VK70xNMC_StartSampling(m_cardId);
    if (result != 0) {
        emitError(QString("VK70xNMC_StartSampling failed: %1").arg(result));
        return false;
    }
    */
    
    m_isSampling = true;
    qDebug() << "[VibrationWorker] Sampling started (SIMULATED)";
    return true;
}

void VibrationWorker::stopSampling()
{
    if (!m_isSampling) {
        return;
    }
    
    qDebug() << "[VibrationWorker] Stopping sampling...";
    
    // TODO: 停止VK701采样
    /*
    VK70xNMC_StopSampling(m_cardId);
    */
    
    m_isSampling = false;
    qDebug() << "[VibrationWorker] Sampling stopped";
}

bool VibrationWorker::readDataBlock()
{
    // TODO: 从VK701读取数据块
    /*
    float *ch0Data = new float[m_blockSize];
    float *ch1Data = new float[m_blockSize];
    float *ch2Data = new float[m_blockSize];
    
    int result = VK70xNMC_GetFourChannel(
        m_cardId, 
        ch0Data, ch1Data, ch2Data, nullptr,  // 3个通道数据
        m_blockSize
    );
    
    if (result != 0) {
        delete[] ch0Data;
        delete[] ch1Data;
        delete[] ch2Data;
        return false;
    }
    
    processAndSendData(ch0Data, ch1Data, ch2Data, m_blockSize);
    
    delete[] ch0Data;
    delete[] ch1Data;
    delete[] ch2Data;
    
    return true;
    */
    
    // 模拟数据读取
    QThread::msleep(static_cast<int>(m_blockSize * 1000.0 / m_sampleRate));
    
    // 模拟发送空数据块
    processAndSendData(nullptr, nullptr, nullptr, m_blockSize);
    
    return true;
}

void VibrationWorker::processAndSendData(float *ch0Data, float *ch1Data, float *ch2Data, int numSamples)
{
    // 计算当前块的起始时间戳
    qint64 elapsedUs = m_timer.nsecsElapsed() / 1000;
    qint64 blockTimestamp = m_baseTimestamp + elapsedUs;
    
    // 为每个通道创建DataBlock并发送
    for (int ch = 0; ch < m_channelCount; ++ch) {
        DataBlock block;
        block.roundId = m_currentRoundId;
        block.channelId = ch;
        block.startTimestampUs = blockTimestamp;
        block.sampleRate = m_sampleRate;
        block.numSamples = numSamples;
        
        // 设置传感器类型
        if (ch == 0) {
            block.sensorType = SensorType::Vibration_X;
        } else if (ch == 1) {
            block.sensorType = SensorType::Vibration_Y;
        } else {
            block.sensorType = SensorType::Vibration_Z;
        }
        
        // 将float数组转换为BLOB
        if (ch0Data && ch1Data && ch2Data) {
            float *channelData = (ch == 0) ? ch0Data : (ch == 1) ? ch1Data : ch2Data;
            block.blobData = QByteArray(
                reinterpret_cast<const char*>(channelData), 
                numSamples * sizeof(float)
            );
        } else {
            // 模拟数据（调试用）
            block.blobData = QByteArray(numSamples * sizeof(float), 0);
        }
        
        // 发送数据块
        emit dataBlockReady(block);
    }
    
    // 更新统计
    m_samplesCollected += numSamples * m_channelCount;
    m_blockSequence++;
    
    // 调试信息（每10秒输出一次）
    if (m_blockSequence % (int)(10 * m_sampleRate / m_blockSize) == 0) {
        qDebug() << QString("[VibrationWorker] Block #%1, Total samples: %2, Rate: %3 Hz")
                    .arg(m_blockSequence)
                    .arg(m_samplesCollected)
                    .arg(m_sampleRate);
    }
}
