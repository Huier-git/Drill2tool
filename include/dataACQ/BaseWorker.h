#ifndef BASEWORKER_H
#define BASEWORKER_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QMutex>
#include <QWaitCondition>
#include <QElapsedTimer>
#include "DataTypes.h"

/**
 * @brief 数据采集Worker基类
 * 
 * 所有采集Worker继承此类，提供统一接口：
 * - start/stop/pause生命周期管理
 * - 统一的dataBlockReady信号
 * - 统一的状态管理和错误处理
 */
class BaseWorker : public QObject
{
    Q_OBJECT
    
public:
    explicit BaseWorker(QObject *parent = nullptr);
    virtual ~BaseWorker();
    
    // 获取当前状态
    WorkerState state() const { return m_state; }
    
    // 获取当前轮次ID
    int currentRoundId() const { return m_currentRoundId; }

public slots:
    /**
     * @brief 启动采集
     */
    virtual void start();
    
    /**
     * @brief 停止采集
     */
    virtual void stop();
    
    /**
     * @brief 暂停采集
     */
    virtual void pause();
    
    /**
     * @brief 恢复采集
     */
    virtual void resume();
    
    /**
     * @brief 设置当前轮次ID
     */
    void setRoundId(int roundId);
    
    /**
     * @brief 设置采样频率
     */
    virtual void setSampleRate(double rate);

    /**
     * @brief 获取采样频率
     */
    double sampleRate() const;

    /**
     * @brief Set base timestamp (us) for aligned sampling.
     */
    void setTimeBase(qint64 baseTimestampUs);

signals:
    /**
     * @brief 数据就绪信号
     * @param block 数据块
     */
    void dataBlockReady(const DataBlock &block);
    
    /**
     * @brief 状态变化信号
     * @param state 新状态
     */
    void stateChanged(WorkerState state);
    
    /**
     * @brief 错误发生信号
     * @param errorMsg 错误信息
     */
    void errorOccurred(const QString &errorMsg);
    
    /**
     * @brief 统计信息信号（用于UI显示）
     * @param samplesCollected 已采集样本数
     * @param currentRate 当前实际采样率
     */
    void statisticsUpdated(qint64 samplesCollected, double currentRate);

    /**
     * @brief 事件信号（用于记录到events表）
     * @param eventType 事件类型
     * @param description 事件描述
     */
    void eventOccurred(const QString &eventType, const QString &description);

protected:
    /**
     * @brief 子类实现：初始化硬件/连接
     */
    virtual bool initializeHardware() = 0;
    
    /**
     * @brief 子类实现：关闭硬件/断开连接
     */
    virtual void shutdownHardware() = 0;
    
    /**
     * @brief 子类实现：采集循环主体
     */
    virtual void runAcquisition() = 0;
    
    /**
     * @brief 改变状态（线程安全）
     */
    void setState(WorkerState newState);
    
    /**
     * @brief 发射错误信号
     */
    void emitError(const QString &errorMsg);
    
    /**
     * @brief 检查是否应该继续运行
     */
    bool shouldContinue() const;

    /**
     * @brief Timestamp helper aligned to the current base time.
     */
    qint64 currentTimestampUs() const;

protected:
    mutable QMutex m_mutex;         // 状态保护互斥锁
    WorkerState m_state;            // 当前状态
    int m_currentRoundId;           // 当前轮次ID
    double m_sampleRate;            // 采样频率
    qint64 m_samplesCollected;      // 已采集样本总数
    bool m_stopRequested;           // 停止请求标志
    qint64 m_timeBaseUs;            // Base timestamp in microseconds
    QElapsedTimer m_elapsedTimer;   // Monotonic timer since base
    bool m_hasTimeBase;             // Whether a base timestamp is set

    // 掉线检测
    int m_consecutiveFails;         // 连续失败计数器
    bool m_connectionLostReported;  // 是否已报告掉线
};

#endif // BASEWORKER_H
