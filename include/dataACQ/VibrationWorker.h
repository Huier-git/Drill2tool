#ifndef VIBRATIONWORKER_H
#define VIBRATIONWORKER_H

#include "dataACQ/BaseWorker.h"
#include <QThread>

/**
 * @brief VK701振动传感器采集Worker
 *
 * 硬件：VK701采集卡
 * 传感器类型：3通道振动传感器（X, Y, Z轴）
 * 默认采样频率：5000Hz（可配置1K-100K Hz）
 * 数据格式：高频BLOB数据
 *
 * 功能：
 * 1. 连接VK701采集卡（TCP通信）
 * 2. 配置采样参数（频率、通道数）
 * 3. 循环采集3通道振动数据（第4通道被忽略）
 * 4. 打包成DataBlock发送
 */
class VibrationWorker : public BaseWorker
{
    Q_OBJECT
    
public:
    explicit VibrationWorker(QObject *parent = nullptr);
    ~VibrationWorker();
    
    // VK701特定配置
    void setCardId(int cardId) { m_cardId = cardId; }
    void setPort(int port) { m_port = port; }
    void setChannelCount(int count) { m_channelCount = count; }
    void setBlockSize(int size) { m_blockSize = size; }
    void setServerAddress(const QString &address) { m_serverAddress = address; }
    
    // 测试连接
    Q_INVOKABLE bool testConnection();
    bool isConnected() const { return m_isCardConnected; }
    void disconnect();

protected:
    // 实现BaseWorker抽象方法
    bool initializeHardware() override;
    void shutdownHardware() override;
    void runAcquisition() override;

private:
    bool connectToCard();
    void disconnectFromCard();
    bool configureChannels();
    bool startSampling();
    void stopSampling();
    bool readDataBlock();
    void processAndSendData(float *ch0Data, float *ch1Data, float *ch2Data, int numSamples);

private:
    int m_cardId;               // 采集卡ID
    int m_port;                 // 网络端口
    QString m_serverAddress;    // 服务器地址（保留用于UI显示）
    int m_channelCount;         // 通道数（固定为3）
    int m_blockSize;            // 每次读取的块大小（点数）

    int m_blockSequence;        // 块序号

    bool m_isCardConnected;     // 是否已连接采集卡
    bool m_isSampling;          // 是否正在采样
};

#endif // VIBRATIONWORKER_H
