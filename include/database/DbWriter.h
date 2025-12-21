#ifndef DBWRITER_H
#define DBWRITER_H

#include <QObject>
#include <QQueue>
#include <QMutex>
#include <QTimer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QMap>
#include "dataACQ/DataTypes.h"

/**
 * @brief 数据库异步写入类
 * 
 * 功能：
 * 1. 接收所有Worker发送的DataBlock
 * 2. 使用队列缓冲
 * 3. 批量事务写入SQLite
 * 4. 流控：队列满时警告或降采样
 * 
 * 重要：此类必须运行在独立线程，保证SQLite线程安全
 */
class DbWriter : public QObject
{
    Q_OBJECT
    
public:
    explicit DbWriter(const QString &dbPath, QObject *parent = nullptr);
    ~DbWriter();
    
    // 获取队列状态
    int queueSize() const;
    int maxQueueSize() const { return m_maxQueueSize; }
    qint64 totalBlocksWritten() const { return m_totalBlocksWritten; }
    
public slots:
    /**
     * @brief 初始化数据库连接（必须在目标线程中调用）
     */
    bool initialize();

    /**
     * @brief 关闭数据库连接
     */
    void shutdown();

    /**
     * @brief 接收数据块（由Worker信号触发）
     */
    void enqueueDataBlock(const DataBlock &block);

    /**
     * @brief 开始新轮次
     * @param operatorName 操作员名称
     * @param note 备注
     * @return 新轮次ID
     */
    int startNewRound(const QString &operatorName = QString(),
                      const QString &note = QString());

    /**
     * @brief 结束当前轮次
     */
    void endCurrentRound();

    /**
     * @brief 清除指定轮次的所有数据
     * @param roundId 要清除的轮次ID
     */
    void clearRoundData(int roundId);

    /**
     * @brief 记录频率变化
     */
    void logFrequencyChange(int roundId, SensorType sensorType,
                           double oldFreq, double newFreq,
                           const QString &comment = QString());

    /**
     * @brief 获取或创建时间窗口
     * @param roundId 轮次ID
     * @param timestampUs 时间戳（微秒）
     * @return 窗口ID，失败返回-1
     */
    int getOrCreateWindow(int roundId, qint64 timestampUs);

signals:
    /**
     * @brief 写入完成信号
     */
    void batchWritten(int numBlocks);
    
    /**
     * @brief 队列警告信号
     */
    void queueWarning(int currentSize, int maxSize);
    
    /**
     * @brief 错误信号
     */
    void errorOccurred(const QString &error);
    
    /**
     * @brief 统计信息
     */
    void statisticsUpdated(qint64 totalBlocks, int queueSize);

private slots:
    /**
     * @brief 定时批量写入
     */
    void processBatch();

private:
    bool initializeDatabase();
    bool createTables();
    bool createTablesManually();
    bool writeScalarData(const DataBlock &block);
    bool writeVibrationData(const DataBlock &block);
    qint64 getCurrentTimestampUs();
    void updateWindowStatus(int windowId, const QString &dataType);
    void clearWindowCache();

private:
    QString m_dbPath;                   // 数据库路径
    QSqlDatabase m_db;                  // 数据库连接
    QQueue<DataBlock> m_queue;          // 数据队列
    mutable QMutex m_queueMutex;        // 队列互斥锁
    QTimer *m_batchTimer;               // 批量写入定时器

    int m_currentRoundId;               // 当前轮次ID
    int m_maxQueueSize;                 // 最大队列长度（默认10000）
    int m_batchSize;                    // 批量大小（默认200）
    int m_batchIntervalMs;              // 批量间隔（默认100ms）

    qint64 m_totalBlocksWritten;        // 已写入总块数
    bool m_isInitialized;               // 是否已初始化

    // 时间窗口管理
    QMap<QPair<int, qint64>, int> m_windowCache;    // 窗口缓存：key=(round_id, window_start_us)
    int m_maxCacheSize;                 // 缓存大小限制（默认100）
};

#endif // DBWRITER_H
