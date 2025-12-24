#include "dataACQ/VibrationWorker.h"
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
    , m_isDllLoaded(false)
    , m_fnTCPOpen(nullptr)
    , m_fnTCPClose(nullptr)
    , m_fnGetConnectedClientNumbers(nullptr)
    , m_fnInitialize(nullptr)
    , m_fnStartSampling(nullptr)
    , m_fnStopSampling(nullptr)
    , m_fnGetFourChannel(nullptr)
{
    m_sampleRate = 5000.0;  // 默认5000Hz
    LOG_DEBUG("VibrationWorker", "Created. Default: 5000Hz, 3 channels, fixed port 8234");
}

VibrationWorker::~VibrationWorker()
{
    if (m_isSampling) {
        stopSampling();
    }
    unloadDll();
}

bool VibrationWorker::loadDll()
{
    if (m_isDllLoaded) {
        return true;
    }

    LOG_DEBUG("VibrationWorker", "Loading VK70xNMC_DAQ2.dll...");

    m_vkLib.setFileName("VK70xNMC_DAQ2.dll");
    if (!m_vkLib.load()) {
        emitError(QString("Failed to load VK70xNMC_DAQ2.dll: %1").arg(m_vkLib.errorString()));
        return false;
    }

    // Resolve all function pointers
    m_fnTCPOpen = (FnServerTCPOpen)m_vkLib.resolve("Server_TCPOpen");
    m_fnTCPClose = (FnServerTCPClose)m_vkLib.resolve("Server_TCPClose");
    m_fnGetConnectedClientNumbers = (FnServerGetConnectedClientNumbers)m_vkLib.resolve("Server_Get_ConnectedClientNumbers");
    m_fnInitialize = (FnVK70xNMCInitialize)m_vkLib.resolve("VK70xNMC_Initialize");
    m_fnStartSampling = (FnVK70xNMCStartSampling)m_vkLib.resolve("VK70xNMC_StartSampling");
    m_fnStopSampling = (FnVK70xNMCStopSampling)m_vkLib.resolve("VK70xNMC_StopSampling");
    m_fnGetFourChannel = (FnVK70xNMCGetFourChannel)m_vkLib.resolve("VK70xNMC_GetFourChannel");

    // Check all functions resolved
    if (!m_fnTCPOpen || !m_fnTCPClose || !m_fnGetConnectedClientNumbers ||
        !m_fnInitialize || !m_fnStartSampling || !m_fnStopSampling || !m_fnGetFourChannel) {
        emitError("Failed to resolve VK70xNMC_DAQ2.dll functions");
        m_vkLib.unload();
        return false;
    }

    m_isDllLoaded = true;
    LOG_DEBUG("VibrationWorker", "VK70xNMC_DAQ2.dll loaded successfully");
    return true;
}

void VibrationWorker::unloadDll()
{
    if (!m_isDllLoaded) {
        return;
    }

    m_fnTCPOpen = nullptr;
    m_fnTCPClose = nullptr;
    m_fnGetConnectedClientNumbers = nullptr;
    m_fnInitialize = nullptr;
    m_fnStartSampling = nullptr;
    m_fnStopSampling = nullptr;
    m_fnGetFourChannel = nullptr;

    m_vkLib.unload();
    m_isDllLoaded = false;
    LOG_DEBUG("VibrationWorker", "VK70xNMC_DAQ2.dll unloaded");
}

bool VibrationWorker::initializeHardware()
{
    LOG_DEBUG("VibrationWorker", "Initializing VK701 hardware...");
    LOG_DEBUG_STREAM("VibrationWorker") << "  Card ID:" << m_cardId;
    LOG_DEBUG_STREAM("VibrationWorker") << "  TCP Port:" << VK701_TCP_PORT << "(fixed)";
    LOG_DEBUG_STREAM("VibrationWorker") << "  Sample Rate:" << m_sampleRate << "Hz";
    LOG_DEBUG_STREAM("VibrationWorker") << "  Channels:" << m_channelCount;

    // Load DLL first
    if (!loadDll()) {
        return false;
    }

    // 连接采集卡（正常模式：无限重试）
    if (!connectToCard(false)) {  // isTestMode = false
        return false;
    }

    // 配置通道（正常模式：无限重试）
    if (!configureChannels(false)) {  // isTestMode = false
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

    // 只停止采样，保持TCP连接（下次启动更快）
    stopSampling();

    // 注意：不断开连接，保持 m_isCardConnected = true
    // 这样下次 initializeHardware() 时会跳过连接步骤

    LOG_DEBUG("VibrationWorker", "VK701 shutdown complete (connection kept)");
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

        // 每次读取后延时（与Linux版本一致，减少CPU占用，让UI有时间处理）
        QThread::msleep(10);

        // Allow queued stop/pause to be processed in this worker thread.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    }

    LOG_DEBUG("VibrationWorker", "Acquisition loop ended");
}

bool VibrationWorker::connectToCard(bool isTestMode)
{
    if (!m_isDllLoaded) {
        emitError("DLL not loaded");
        return false;
    }

    // 如果已经连接，跳过连接步骤
    if (m_isCardConnected) {
        LOG_DEBUG("VibrationWorker", "Already connected to VK701, skipping connection");
        return true;
    }

    LOG_DEBUG_STREAM("VibrationWorker") << "Connecting to VK701 TCP server, port:" << VK701_TCP_PORT;

    int result;
    int curDeviceNum;
    int retryCount = 0;
    const int maxRetries = 100;  // 测试模式最多重试100次

    // 1. 创建TCP连接（与例程一致：无限重试直到成功）
    do {
        result = m_fnTCPOpen(VK701_TCP_PORT);
        QThread::msleep(20);
        if (result < 0) {
            LOG_DEBUG("VibrationWorker", "Waiting for VK701 server...");
            retryCount++;
            if (isTestMode && retryCount >= maxRetries) {
                emitError(QString("Failed to open VK701 TCP server port after %1 attempts").arg(maxRetries));
                return false;
            }
        } else {
            LOG_DEBUG_STREAM("VibrationWorker") << "Port" << VK701_TCP_PORT << "opened!";
        }
    } while (result < 0);

    QThread::msleep(500);  // 等待TCP服务器完全就绪（参考例程）

    // 2. 获取已连接设备数量（与例程一致：无限重试直到成功）
    LOG_DEBUG("VibrationWorker", "Getting connected device count...");
    retryCount = 0;
    do {
        result = m_fnGetConnectedClientNumbers(&curDeviceNum);
        QThread::msleep(20);
        if (result < 0) {
            LOG_DEBUG("VibrationWorker", "Waiting for device enumeration...");
            retryCount++;
            if (isTestMode && retryCount >= maxRetries) {
                emitError(QString("Failed to get device count after %1 attempts").arg(maxRetries));
                return false;
            }
        }
    } while (result < 0);

    LOG_DEBUG_STREAM("VibrationWorker") << "DAQ device count:" << curDeviceNum;

    // 检查是否有设备连接
    if (curDeviceNum <= 0) {
        emitError("No VK701 device connected to server");
        LOG_WARNING("VibrationWorker", "Server opened but no device connected");
        return false;
    }

    QThread::msleep(500);  // 等待设备枚举完全完成（参考例程）

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

    // 注意：不关闭TCP连接，多个连接可以共享同一个服务器

    m_isCardConnected = false;
    LOG_DEBUG("VibrationWorker", "Disconnected");
}

bool VibrationWorker::testConnection()
{
    LOG_DEBUG("VibrationWorker", "Testing connection to VK701...");

    // Load DLL first
    if (!loadDll()) {
        return false;
    }

    // 尝试连接（测试模式：有限重试）
    if (!connectToCard(true)) {  // isTestMode = true
        return false;
    }

    LOG_DEBUG("VibrationWorker", "Connection test successful");
    return true;
}

void VibrationWorker::disconnect()
{
    disconnectFromCard();
}

bool VibrationWorker::configureChannels(bool isTestMode)
{
    if (!m_isDllLoaded) {
        emitError("DLL not loaded");
        return false;
    }

    LOG_DEBUG("VibrationWorker", "Configuring VK701 channels...");

    int result;
    int retryCount = 0;
    const int maxRetries = 100;  // 测试模式最多重试100次

    // VK701初始化参数（参考vk701nsd）
    double refVol = 1.0;           // 参考电压
    int bitMode = 2;               // 采样分辨率 (24位)
    int volRange = 0;              // 电压输入范围

    LOG_DEBUG("VibrationWorker", "Initializing VK701 device...");
    LOG_DEBUG_STREAM("VibrationWorker") << "  Card ID:" << m_cardId;
    LOG_DEBUG_STREAM("VibrationWorker") << "  Sample Rate:" << static_cast<int>(m_sampleRate) << "Hz";
    LOG_DEBUG_STREAM("VibrationWorker") << "  Ref Voltage:" << refVol << "V";
    LOG_DEBUG_STREAM("VibrationWorker") << "  Bit Mode:" << bitMode;

    // 初始化设备（与例程一致：无限重试直到成功）
    do {
        result = m_fnInitialize(
            m_cardId,
            refVol,
            bitMode,
            static_cast<int>(m_sampleRate),
            volRange, volRange, volRange, volRange  // 4个通道的电压范围
        );

        QThread::msleep(20);

        if (result == -11) {
            LOG_DEBUG("VibrationWorker", "Server not open.");
        } else if (result == -12 || result == -13) {
            LOG_DEBUG("VibrationWorker", "DAQ not connected or does not exist.");
        } else if (result < 0) {
            LOG_DEBUG("VibrationWorker", "Initialization error.");
        }

        retryCount++;
        if (isTestMode && retryCount >= maxRetries) {
            emitError(QString("VK70xNMC_Initialize failed after %1 attempts: error code %2").arg(maxRetries).arg(result));
            return false;
        }
    } while (result < 0);

    QThread::msleep(500);  // 等待设备初始化完全完成（参考例程）
    LOG_DEBUG("VibrationWorker", "VK701 device initialized successfully");
    return true;
}

bool VibrationWorker::startSampling()
{
    if (!m_isDllLoaded) {
        emitError("DLL not loaded");
        return false;
    }

    LOG_DEBUG("VibrationWorker", "Starting VK701 sampling...");

    int result = m_fnStartSampling(m_cardId);
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
    if (!m_isSampling || !m_isDllLoaded) {
        return;
    }

    LOG_DEBUG("VibrationWorker", "Stopping VK701 sampling...");

    int result = m_fnStopSampling(m_cardId);
    if (result < 0) {
        LOG_WARNING_STREAM("VibrationWorker") << "VK70xNMC_StopSampling failed: error code" << result;
    }

    m_isSampling = false;
    LOG_DEBUG("VibrationWorker", "Sampling stopped");
}

bool VibrationWorker::readDataBlock()
{
    if (!m_isDllLoaded) {
        return false;
    }

    // 使用采样频率作为读取点数（与例程和Linux版本一致）
    int readSize = static_cast<int>(m_sampleRate);

    // 分配缓冲区用于接收4通道数据（VK701硬件限制）
    double *pucRecBuf = new double[4 * readSize];

    // Start sampling if needed to avoid restarting every read.
    if (!m_isSampling) {
        int result = m_fnStartSampling(m_cardId);
        if (result < 0) {
            LOG_WARNING_STREAM("VibrationWorker") << "VK70xNMC_StartSampling failed before read: error code" << result;
            delete[] pucRecBuf;
            return false;
        }
        m_isSampling = true;
    }

    // 读取4通道数据（第三个参数使用采样频率，与例程一致）
    int recv = m_fnGetFourChannel(m_cardId, pucRecBuf, readSize);

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
        // recv == 0，没有数据可读（与例程不同：例程中会在循环中等待）
        delete[] pucRecBuf;
        QThread::msleep(1);  // 短暂延迟后重试（参考例程：1ms）
        return true;
    }
}

void VibrationWorker::processAndSendData(float *ch0Data, float *ch1Data, float *ch2Data, int numSamples)
{
    // 检查数据有效性
    if (!ch0Data || !ch1Data || !ch2Data || numSamples <= 0) {
        return;
    }

    // 单位换算：传感器灵敏度 100mV/g = 0.1V/g
    // VK701返回电压值(V)，需要转换为加速度(g)
    // g = V / 0.1 = V * 10
    const float sensitivity = 0.1f;  // 100mV/g = 0.1V/g

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

        // 选择对应通道数据并换算单位
        float *channelData = (ch == 0) ? ch0Data : (ch == 1) ? ch1Data : ch2Data;

        // 创建换算后的数据缓冲区
        QVector<float> convertedData(numSamples);
        for (int i = 0; i < numSamples; ++i) {
            convertedData[i] = channelData[i] / sensitivity;  // V → g
        }

        // 将换算后的float数组转换为BLOB
        block.blobData = QByteArray(
            reinterpret_cast<const char*>(convertedData.constData()),
            numSamples * sizeof(float)
        );

        // 发送数据块
        emit dataBlockReady(block);
    }

    // 更新统计
    m_samplesCollected += numSamples * m_channelCount;
    m_blockSequence++;

    // 调试信息（每10个块输出一次，约10秒）
    if (m_blockSequence % 10 == 0) {
        LOG_DEBUG_STREAM("VibrationWorker")
            << "Block #" << m_blockSequence
            << ", Samples this block:" << numSamples
            << ", Total samples:" << m_samplesCollected
            << ", Rate:" << m_sampleRate << "Hz";
    }
}
