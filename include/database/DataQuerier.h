#ifndef DATAQUERIER_H
#define DATAQUERIER_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVector>
#include <QList>
#include <QMap>
#include "dataACQ/DataTypes.h"

/**
 * @brief 数据查询类 - 查询多频率对齐的传感器数据
 *
 * 核心功能：
 * 1. 按时间窗口查询数据（1秒窗口对齐）
 * 2. 查询某1秒内的所有数据（5000个振动点 + 10个MDB点 + 100个电机点）
 * 3. 简洁高效的查询接口
 */
class DataQuerier : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 窗口数据结构 - 某1秒内的所有数据
     */
    struct WindowData {
        qint64 windowStartUs;                               // 窗口起始时间（微秒）
        QMap<int, QVector<float>> vibrationData;            // key=channelId(0/1/2), value=振动数据数组
        QMap<int, QVector<double>> scalarData;              // key=sensorType, value=标量数据数组

        WindowData() : windowStartUs(0) {}
    };

    /**
     * @brief 轮次信息
     */
    struct RoundInfo {
        int roundId;
        qint64 startTimeUs;
        qint64 endTimeUs;
        QString status;
        QString operatorName;
        QString note;

        RoundInfo() : roundId(0), startTimeUs(0), endTimeUs(0) {}
    };

public:
    explicit DataQuerier(const QString &dbPath, QObject *parent = nullptr);
    ~DataQuerier();

    /**
     * @brief 初始化数据库连接
     */
    bool initialize();

    /**
     * @brief 关闭数据库连接
     */
    void close();

    /**
     * @brief 获取所有轮次列表
     */
    QList<RoundInfo> getAllRounds();

    /**
     * @brief 获取指定轮次的窗口时间戳列表
     * @param roundId 轮次ID
     * @return 窗口起始时间戳列表（微秒）
     */
    QList<qint64> getWindowTimestamps(int roundId);

    /**
     * @brief 查询指定窗口的所有数据（核心功能）
     * @param roundId 轮次ID
     * @param windowStartUs 窗口起始时间（微秒）
     * @return 该窗口内的所有数据
     */
    WindowData getWindowData(int roundId, qint64 windowStartUs);

    /**
     * @brief 查询时间范围内的所有窗口数据
     * @param roundId 轮次ID
     * @param startTimeUs 起始时间（微秒）
     * @param endTimeUs 结束时间（微秒）
     * @return 窗口数据列表
     */
    QList<WindowData> getTimeRangeData(int roundId, qint64 startTimeUs, qint64 endTimeUs);

    /**
     * @brief 获取振动数据的统计信息（不解析BLOB，直接读预计算值）
     */
    struct VibrationStats {
        qint64 timestampUs;
        float minValue;
        float maxValue;
        float meanValue;
        float rmsValue;
    };
    QList<VibrationStats> getVibrationStats(int roundId, int channelId,
                                            qint64 startTimeUs, qint64 endTimeUs);

    /**
     * @brief 获取轮次的实际数据时长（从实际数据时间戳计算）
     * @param roundId 轮次ID
     * @return 时长（秒），如果无数据返回0
     */
    qint64 getRoundActualDuration(int roundId);

    /**
     * @brief 获取数据库连接，供自定义SQL查询使用
     */
    QSqlDatabase database() const { return m_db; }

signals:
    void errorOccurred(const QString &error);

private:
    QString m_dbPath;
    QSqlDatabase m_db;
    bool m_isInitialized;
};

#endif // DATAQUERIER_H
