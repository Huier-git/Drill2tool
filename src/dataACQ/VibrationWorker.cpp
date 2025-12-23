#include "dataACQ/VibrationWorker.h"
#include "VK70xNMC_DAQ2.h"  // VK701硬件库头文件
#include "Logger.h"
#include <QDebug>
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

VibrationWorker::VibrationWorker(QObject *parent)
    : BaseWorker(parent)
    , m_cardId(0)
    , m_channelCount(3)
    , m_blockSize(1000)
    , m_blockSequence(0)
    , m_isCardConnected(false)
    , m_isSampling(false)
{
    m_sampleRate = 5000.0;  // 默认5000Hz
    LOG_DEBUG("VibrationWorker", "Created. Default: 5000Hz, 3 channels, fixed port 8234");
}

VibrationWorker::~VibrationWorker()
{
    if (m_isSampling) {
        stopSampling();
    }
}

bool VibrationWorker::initializeHardware()
{
    LOG_DEBUG("VibrationWorker", "Initializing VK701 hardware...");
    LOG_DEBUG_STREAM("VibrationWorker") << "  Card ID:" << m_cardId;
    LOG_DEBUG_STREAM("VibrationWorker") << "  TCP Port:" << VK701_TCP_PORT << "(fixed)";
    LOG_DEBUG_STREAM("VibrationWorker") << "  Sample Rate:" << m_sampleRate << "Hz";
    LOG_DEBUG_STREAM("VibrationWorker") << "  Channels:" << m_channelCount;

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

    m_blockSequence = 0;

    LOG_DEBUG("VibrationWorker", "Hardware initialized successfully");
    return true;
}

void VibrationWorker::shutdownHardware()
{
    LOG_DEBUG("VibrationWorker", "Shutting down VK701...");

    // 停止采样
    stopSampling();

    // 断开连接
    disconnectFromCard();

    m_isCardConnected = false;
    m_isSampling = false;

    LOG_DEBUG("VibrationWorker", "VK701 shutdown complete");
}

void VibrationWorker::runAcquisition()
{
    LOG_DEBUG("VibrationWorker", "Acquisition loop started");

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

        // Allow queued stop/pause to be processed in this worker thread.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    }

    LOG_DEBUG("VibrationWorker", "Acquisition loop ended");
}

bool VibrationWorker::connectToCard()
{
    LOG_DEBUG_STREAM("VibrationWorker") << "Connecting to VK701 TCP server, port:" << VK701_TCP_PORT;

    int result;
    int curDeviceNum;

    // 1. 创建TCP连接
    do {
        if (!shouldContinue()) {
            return false;
        }
        result = Server_TCPOpen(VK701_TCP_PORT);
        QThread::msleep(20);
        if (result < 0) {
            LOG_DEBUG("VibrationWorker", "Waiting for VK701 server...");
        } else {
            LOG_DEBUG_STREAM("VibrationWorker") << "Port" << VK701_TCP_PORT << "opened!";
        }
    } while (result < 0 && shouldContinue());

    if (result < 0) {
        emitError("Failed to open VK701 TCP server port");
        return false;
    }

    QThread::msleep(100);

    // 2. 获取已连接设备数量
    LOG_DEBUG("VibrationWorker", "Getting connected device count...");
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

    LOG_DEBUG_STREAM("VibrationWorker") << "DAQ device count:" << curDeviceNum;
    QThread::msleep(100);

    m_isCardConnected = true;
    LOG_DEBUG("VibrationWorker", "Successfully connected to VK701 server");
    return true;
}

void VibrationWorker::disconnectFromCard()
{
    if (!m_isCardConnected) {
        return;
    }

    LOG_DEBUG("VibrationWorker", "Disconnecting from VK701...");

    // 停止采样
    stopSampling();

    // VK701不需要显式关闭TCP连接，由SDK管理
    // 如果需要关闭服务器,调用 Server_TCPClose(m_port)

    m_isCardConnected = false;
    LOG_DEBUG("VibrationWorker", "Disconnected");
}

bool VibrationWorker::testConnection()
{
    LOG_DEBUG("VibrationWorker", "Testing connection to VK701...");

    // 尝试连接
    if (!connectToCard()) {
        return false;
    }

    LOG_DEBUG("VibrationWorker", "Connection test successful");
    return true;
}

void VibrationWorker::disconnect()
{
    disconnectFromCard();
}

bool VibrationWorker::configureChannels()
{
    LOG_DEBUG("VibrationWorker", "Configuring VK701 channels...");

    int result;
    int retryCount = 0;
    const int maxRetries = 2000;  // 最多重试次数

    // VK701初始化参数（参考vk701nsd）
    double refVol = 1.0;           // 参考电压
    int bitMode = 2;               // 采样分辨率 (24位)
    int volRange = 0;              // 电压输入范围

    LOG_DEBUG("VibrationWorker", "Initializing VK701 device...");
    LOG_DEBUG_STREAM("VibrationWorker") << "  Card ID:" << m_cardId;
    LOG_DEBUG_STREAM("VibrationWorker") << "  Sample Rate:" << static_cast<int>(m_sampleRate) << "Hz";
    LOG_DEBUG_STREAM("VibrationWorker") << "  Ref Voltage:" << refVol << "V";
    LOG_DEBUG_STREAM("VibrationWorker") << "  Bit Mode:" << bitMode;

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
                LOG_DEBUG("VibrationWorker", "Server not open.");
            } else if (result == -12 || result == -13) {
                LOG_DEBUG_STREAM("VibrationWorker") << "DAQ not connected or does not exist. Try" << retryCount;
            } else if (result < 0) {
                LOG_DEBUG_STREAM("VibrationWorker") << "Initialization error. Try" << retryCount;
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
    LOG_DEBUG("VibrationWorker", "VK701 device initialized successfully");
    return true;
}

bool VibrationWorker::startSampling()
{
    LOG_DEBUG("VibrationWorker", "Starting VK701 sampling...");

    int result = VK70xNMC_StartSampling(m_cardId);
    if (result < 0) {
        emitError(QString("VK70xNMC_StartSampling failed: error code %1").arg(result));
        LOG_DEBUG("VibrationWorker", "DAQ ERROR: Failed to start sampling");
        return false;
    }

    m_isSampling = true;
    LOG_DEBUG("VibrationWorker", "VK701 sampling started successfully");
    return true;
}

void VibrationWorker::stopSampling()
{
    if (!m_isSampling) {
        return;
    }

    LOG_DEBUG("VibrationWorker", "Stopping VK701 sampling...");

    int result = VK70xNMC_StopSampling(m_cardId);
    if (result < 0) {
        LOG_WARNING_STREAM("VibrationWorker") << "VK70xNMC_StopSampling failed: error code" << result;
    }

    m_isSampling = false;
    LOG_DEBUG("VibrationWorker", "Sampling stopped");
}

bool VibrationWorker::readDataBlock()
{
    // 分配缓冲区用于接收4通道数据（VK701硬件限制）
    double *pucRecBuf = new double[4 * m_blockSize];

    // Start sampling if needed to avoid restarting every read.
    if (!m_isSampling) {
        int result = VK70xNMC_StartSampling(m_cardId);
        if (result < 0) {
            LOG_WARNING_STREAM("VibrationWorker") << "VK70xNMC_StartSampling failed before read: error code" << result;
            delete[] pucRecBuf;
            return false;
        }
        m_isSampling = true;
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

        // 成功读取，重置失败计数器
        if (m_consecutiveFails > 0) {
            if (m_connectionLostReported) {
                emit eventOccurred("VK701SensorReconnected",
                                  QString("VK701传感器恢复连接（失败计数: %1）").arg(m_consecutiveFails));
                LOG_DEBUG("VibrationWorker", "VK701 sensor reconnected");
                m_connectionLostReported = false;
            }
            m_consecutiveFails = 0;
        }

        return true;
    } else if (recv < 0) {
        // 读取错误 - 掉线检测
        m_consecutiveFails++;
        if (m_consecutiveFails == 20 && !m_connectionLostReported) {
            // 连续20次失败（约1秒，因为采集很快），报告掉线
            emit eventOccurred("VK701SensorDisconnected",
                              QString("VK701传感器连续%1次读取失败，可能已掉线").arg(m_consecutiveFails));
            m_connectionLostReported = true;
            LOG_WARNING_STREAM("VibrationWorker") << "VK701 sensor appears disconnected (" << m_consecutiveFails << " consecutive failures)";
        }

        LOG_WARNING_STREAM("VibrationWorker") << "VK70xNMC_GetFourChannel failed: error code" << recv;
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
    qint64 blockTimestamp = currentTimestampUs();

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
        LOG_DEBUG_STREAM("VibrationWorker")
            << "Block #" << m_blockSequence
            << ", Total samples:" << m_samplesCollected
            << ", Rate:" << m_sampleRate << "Hz";
    }
}
