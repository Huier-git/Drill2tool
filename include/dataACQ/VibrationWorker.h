#ifndef VIBRATIONWORKER_H
#define VIBRATIONWORKER_H

#include "dataACQ/BaseWorker.h"
#include <QThread>
#include <QLibrary>

/**
 * @brief VK701振动传感器采集Worker
 *
 * 硬件：VK701采集卡（TCP端口：8234固定）
 * 传感器类型：3通道振动传感器（X, Y, Z轴）
 * 默认采样频率：5000Hz（可配置1K-100K Hz）
 * 数据格式：高频BLOB数据
 *
 * 功能：
 * 1. 连接VK701采集卡（固定TCP端口8234）
 * 2. 配置采样参数（频率、通道数）
 * 3. 循环采集3通道振动数据（第4通道被忽略）
 * 4. 打包成DataBlock发送
 *
 * 连接参数：
 * - cardId: 卡号（0-7）
 * - port: 固定为8234（无需配置）
 *
 * 注意：使用QLibrary动态加载VK70xNMC_DAQ2.dll，与官方例程一致
 */
class VibrationWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit VibrationWorker(QObject *parent = nullptr);
    ~VibrationWorker();

    // VK701特定配置
    void setCardId(int cardId) { m_cardId = cardId; }
    void setChannelCount(int count) { m_channelCount = count; }
    void setBlockSize(int size) { m_blockSize = size; }

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
    bool loadDll();
    void unloadDll();
    bool connectToCard(bool isTestMode = false);
    void disconnectFromCard();
    bool configureChannels(bool isTestMode = false);
    bool startSampling();
    void stopSampling();
    bool readDataBlock();
    void processAndSendData(float *ch0Data, float *ch1Data, float *ch2Data, int numSamples);

private:
    static constexpr int VK701_TCP_PORT = 8234;  // VK701固定TCP端口
    int m_cardId;               // 采集卡ID（0-7）
    int m_channelCount;         // 通道数（固定为3）
    int m_blockSize;            // 每次读取的块大小（点数）

    int m_blockSequence;        // 块序号

    bool m_isCardConnected;     // 是否已连接采集卡
    bool m_isSampling;          // 是否正在采样
    bool m_isDllLoaded;         // DLL是否已加载

    // QLibrary动态加载
    QLibrary m_vkLib;

    // 函数指针类型（与官方例程一致，使用__stdcall）
    typedef int (__stdcall *FnServerTCPOpen)(int);
    typedef int (__stdcall *FnServerTCPClose)(int);
    typedef int (__stdcall *FnServerGetConnectedClientNumbers)(int*);
    typedef int (__stdcall *FnVK70xNMCInitialize)(int, double, int, int, int, int, int, int);
    typedef int (__stdcall *FnVK70xNMCStartSampling)(int);
    typedef int (__stdcall *FnVK70xNMCStopSampling)(int);
    typedef int (__stdcall *FnVK70xNMCGetFourChannel)(int, double*, int);

    // 函数指针
    FnServerTCPOpen m_fnTCPOpen;
    FnServerTCPClose m_fnTCPClose;
    FnServerGetConnectedClientNumbers m_fnGetConnectedClientNumbers;
    FnVK70xNMCInitialize m_fnInitialize;
    FnVK70xNMCStartSampling m_fnStartSampling;
    FnVK70xNMCStopSampling m_fnStopSampling;
    FnVK70xNMCGetFourChannel m_fnGetFourChannel;
};

#endif // VIBRATIONWORKER_H
