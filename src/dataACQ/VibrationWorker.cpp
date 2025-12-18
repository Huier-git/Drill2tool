#include "dataACQ/VibrationWorker.h"
#include "VK70xNMC_DAQ2.h"  // VK701硬件库头文件
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
    qDebug() << "[VibrationWorker] Shutting down VK701...";

    // 停止采样
    stopSampling();

    // 断开连接
    disconnectFromCard();

    m_isCardConnected = false;
    m_isSampling = false;

    qDebug() << "[VibrationWorker] VK701 shutdown complete";
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
    qDebug() << "[VibrationWorker] Connecting to VK701 TCP server, port:" << m_port;

    int result;
    int curDeviceNum;

    // 1. 创建TCP连接
    do {
        if (!shouldContinue()) {
            return false;
        }
        result = Server_TCPOpen(m_port);
        QThread::msleep(20);
        if (result < 0) {
            qDebug() << "[VibrationWorker] Waiting for VK701 server...";
        } else {
            qDebug() << "[VibrationWorker] Port" << m_port << "opened!";
        }
    } while (result < 0 && shouldContinue());

    if (result < 0) {
        emitError("Failed to open VK701 TCP server port");
        return false;
    }

    QThread::msleep(100);

    // 2. 获取已连接设备数量
    qDebug() << "[VibrationWorker] Getting connected device count...";
    int retryCount = 0;
    do {
        if (!shouldContinue()) {
            return false;
        }
        result = Server_Get_ConnectedClientNumbers(&curDeviceNum);
        QThread::msleep(20);
        retryCount++;
    } while (result < 0 && retryCount < 50 && shouldContinue());

    if (result < 0) {
        emitError("Failed to get VK701 device count");
        return false;
    }

    qDebug() << "[VibrationWorker] DAQ device count:" << curDeviceNum;
    QThread::msleep(100);

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

    // 停止采样
    stopSampling();

    // VK701不需要显式关闭TCP连接，由SDK管理
    // 如果需要关闭服务器，调用 Server_TCPClose(m_port)

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

    qDebug() << "[VibrationWorker] Connection test successful";
    return true;
}

void VibrationWorker::disconnect()
{
    disconnectFromCard();
}

bool VibrationWorker::configureChannels()
{
    qDebug() << "[VibrationWorker] Configuring VK701 channels...";

    int result;
    int retryCount = 0;
    const int maxRetries = 2000;  // 最多重试次数

    // VK701初始化参数（参考vk701nsd）
    double refVol = 1.0;           // 参考电压
    int bitMode = 2;               // 采样分辨率 (24位)
    int volRange = 0;              // 电压输入范围

    qDebug() << "[VibrationWorker] Initializing VK701 device...";
    qDebug() << "  Card ID:" << m_cardId;
    qDebug() << "  Sample Rate:" << static_cast<int>(m_sampleRate) << "Hz";
    qDebug() << "  Ref Voltage:" << refVol << "V";
    qDebug() << "  Bit Mode:" << bitMode;

    do {
        if (!shouldContinue()) {
            return false;
        }

        result = VK70xNMC_Initialize(
            m_cardId,
            refVol,
            bitMode,
            static_cast<int>(m_sampleRate),
            volRange, volRange, volRange, volRange  // 4个通道的电压范围
        );

        QThread::msleep(20);

        if (retryCount < maxRetries) {
            if (result == -11) {
                qDebug() << "[VibrationWorker] Server not open.";
            } else if (result == -12 || result == -13) {
                qDebug() << "[VibrationWorker] DAQ not connected or does not exist. Try" << retryCount;
            } else if (result < 0) {
                qDebug() << "[VibrationWorker] Initialization error. Try" << retryCount;
            }
        }
        retryCount++;
    } while (result < 0 && retryCount < maxRetries && shouldContinue());

    if (result < 0) {
        emitError(QString("VK70xNMC_Initialize failed after %1 attempts: error code %2")
                      .arg(maxRetries).arg(result));
        return false;
    }

    QThread::msleep(100);
    qDebug() << "[VibrationWorker] VK701 device initialized successfully";
    return true;
}

bool VibrationWorker::startSampling()
{
    qDebug() << "[VibrationWorker] Starting VK701 sampling...";

    int result = VK70xNMC_StartSampling(m_cardId);
    if (result < 0) {
        emitError(QString("VK70xNMC_StartSampling failed: error code %1").arg(result));
        qDebug() << "[VibrationWorker] DAQ ERROR: Failed to start sampling";
        return false;
    }

    m_isSampling = true;
    qDebug() << "[VibrationWorker] VK701 sampling started successfully";
    return true;
}

void VibrationWorker::stopSampling()
{
    if (!m_isSampling) {
        return;
    }

    qDebug() << "[VibrationWorker] Stopping VK701 sampling...";

    int result = VK70xNMC_StopSampling(m_cardId);
    if (result < 0) {
        qWarning() << "[VibrationWorker] VK70xNMC_StopSampling failed: error code" << result;
    }

    m_isSampling = false;
    qDebug() << "[VibrationWorker] Sampling stopped";
}

bool VibrationWorker::readDataBlock()
{
    // 分配缓冲区用于接收4通道数据（VK701硬件限制）
    double *pucRecBuf = new double[4 * m_blockSize];

    // 启动采样
    int result = VK70xNMC_StartSampling(m_cardId);
    if (result < 0) {
        qWarning() << "[VibrationWorker] VK70xNMC_StartSampling failed before read: error code" << result;
        delete[] pucRecBuf;
        return false;
    }

    // 读取4通道数据
    int recv = VK70xNMC_GetFourChannel(m_cardId, pucRecBuf, m_blockSize);

    if (recv > 0) {
        // 成功读取数据
        // recv 是每个通道实际读取的采样点数
        // pucRecBuf 存储格式：[ch0[0], ch1[0], ch2[0], ch3[0], ch0[1], ch1[1], ...]

        // 分离3个通道的数据（忽略第4通道）
        float *ch0Data = new float[recv];
        float *ch1Data = new float[recv];
        float *ch2Data = new float[recv];

        for (int i = 0; i < recv; i++) {
            ch0Data[i] = static_cast<float>(pucRecBuf[i * 4 + 0]);
            ch1Data[i] = static_cast<float>(pucRecBuf[i * 4 + 1]);
            ch2Data[i] = static_cast<float>(pucRecBuf[i * 4 + 2]);
            // 第4通道 (pucRecBuf[i * 4 + 3]) 被忽略
        }

        // 处理并发送数据
        processAndSendData(ch0Data, ch1Data, ch2Data, recv);

        delete[] ch0Data;
        delete[] ch1Data;
        delete[] ch2Data;
        delete[] pucRecBuf;

        return true;
    } else if (recv < 0) {
        // 读取错误
        qWarning() << "[VibrationWorker] VK70xNMC_GetFourChannel failed: error code" << recv;
        delete[] pucRecBuf;
        return false;
    } else {
        // recv == 0，没有数据可读
        delete[] pucRecBuf;
        QThread::msleep(10);  // 短暂延迟后重试
        return true;
    }
}

void VibrationWorker::processAndSendData(float *ch0Data, float *ch1Data, float *ch2Data, int numSamples)
{
    // 检查数据有效性
    if (!ch0Data || !ch1Data || !ch2Data || numSamples <= 0) {
        return;
    }

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
        float *channelData = (ch == 0) ? ch0Data : (ch == 1) ? ch1Data : ch2Data;
        block.blobData = QByteArray(
            reinterpret_cast<const char*>(channelData),
            numSamples * sizeof(float)
        );

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
