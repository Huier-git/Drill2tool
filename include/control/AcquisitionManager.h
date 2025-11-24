#ifndef ACQUISITIONMANAGER_H
#define ACQUISITIONMANAGER_H

#include <QObject>
#include <QThread>
#include "dataACQ/DataTypes.h"

// 前向声明
class BaseWorker;
class VibrationWorker;
class MdbWorker;
class MotorWorker;
class DbWriter;

/**
 * @brief 数据采集统一管理器
 * 
 * 职责：
 * 1. 创建和管理所有Worker + 线程
 * 2. 创建和管理DbWriter + 线程
 * 3. 提供统一的启动/停止接口
 * 4. 管理round_id生命周期
 * 5. 连接Worker信号到DbWriter
 * 6. 统一的错误处理和状态通知
 * 
 * 使用方式：
 * ```cpp
 * AcquisitionManager *manager = new AcquisitionManager(this);
 * manager->initialize();
 * manager->startAll();  // 开始采集
 * manager->stopAll();   // 停止采集
 * ```
 */
class AcquisitionManager : public QObject
{
    Q_OBJECT
    
public:
    explicit AcquisitionManager(QObject *parent = nullptr);
    ~AcquisitionManager();
    
    // 初始化/清理
    bool initialize(const QString &dbPath = "database/drill_data.db");
    void shutdown();
    
    // 获取Worker实例（用于配置）
    VibrationWorker* vibrationWorker() { return m_vibrationWorker; }
    MdbWorker* mdbWorker() { return m_mdbWorker; }
    MotorWorker* motorWorker() { return m_motorWorker; }
    DbWriter* dbWriter() { return m_dbWriter; }
    
    // 获取当前状态
    int currentRoundId() const { return m_currentRoundId; }
    bool isRunning() const { return m_isRunning; }

public slots:
    /**
     * @brief 启动所有采集
     */
    void startAll();
    
    /**
     * @brief 停止所有采集
     */
    void stopAll();
    
    /**
     * @brief 启动单个Worker
     */
    void startVibration();
    void startMdb();
    void startMotor();
    
    /**
     * @brief 停止单个Worker
     */
    void stopVibration();
    void stopMdb();
    void stopMotor();
    
    /**
     * @brief 开始新轮次
     */
    void startNewRound(const QString &operatorName = QString(), 
                       const QString &note = QString());
    
    /**
     * @brief 结束当前轮次
     */
    void endCurrentRound();

signals:
    /**
     * @brief 采集状态变化
     */
    void acquisitionStateChanged(bool isRunning);
    
    /**
     * @brief 轮次变化
     */
    void roundChanged(int roundId);
    
    /**
     * @brief 错误发生
     */
    void errorOccurred(const QString &workerName, const QString &error);
    
    /**
     * @brief 统计信息（定期发送）
     */
    void statisticsUpdated(const QString &info);

private:
    void setupWorkers();
    void setupThreads();
    void connectSignals();
    void cleanupThreads();

private:
    // Worker实例
    VibrationWorker *m_vibrationWorker;
    MdbWorker *m_mdbWorker;
    MotorWorker *m_motorWorker;
    DbWriter *m_dbWriter;
    
    // 线程实例
    QThread *m_vibrationThread;
    QThread *m_mdbThread;
    QThread *m_motorThread;
    QThread *m_dbThread;
    
    // 状态
    int m_currentRoundId;
    bool m_isRunning;
    bool m_isInitialized;
    
    QString m_dbPath;
};

#endif // ACQUISITIONMANAGER_H
