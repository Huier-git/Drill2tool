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
 * 注意：
 * - 运动控制由 ZMotionDriver 和 MotionLockManager 统一管理
 * - 此类只负责数据采集，不负责运动控制
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
    QString dbPath() const { return m_dbPath; }

public slots:
    void startAll();
    void stopAll();
    void startVibration();
    void startMdb();
    void startMotor();
    void stopVibration();
    void stopMdb();
    void stopMotor();
    void startNewRound(const QString &operatorName = QString(),
                       const QString &note = QString());
    void endCurrentRound();
    void resetCurrentRound(int targetRound);

signals:
    void acquisitionStateChanged(bool isRunning);
    void roundChanged(int roundId);
    void errorOccurred(const QString &workerName, const QString &error);
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
