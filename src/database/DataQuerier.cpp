#include "database/DataQuerier.h"
#include <QSqlError>
#include <QDebug>
#include <QThread>

DataQuerier::DataQuerier(const QString &dbPath, QObject *parent)
    : QObject(parent)
    , m_dbPath(dbPath)
    , m_isInitialized(false)
{
}

DataQuerier::~DataQuerier()
{
    close();
}

bool DataQuerier::initialize()
{
    if (m_isInitialized) {
        return true;
    }

    // 创建数据库连接（使用线程ID作为连接名避免冲突）
    QString connectionName = QString("DataQuerier_%1").arg((qint64)QThread::currentThreadId());
    m_db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        emit errorOccurred("Failed to open database: " + m_db.lastError().text());
        return false;
    }

    m_isInitialized = true;
    qDebug() << "DataQuerier initialized:" << m_dbPath;
    return true;
}

void DataQuerier::close()
{
    if (m_isInitialized && m_db.isOpen()) {
        m_db.close();
        m_isInitialized = false;
    }
}

QList<DataQuerier::RoundInfo> DataQuerier::getAllRounds()
{
    QList<RoundInfo> rounds;

    if (!m_isInitialized) {
        qWarning() << "DataQuerier not initialized";
        return rounds;
    }

    QSqlQuery query(m_db);
    query.prepare("SELECT round_id, start_ts_us, end_ts_us, status, operator_name, note "
                  "FROM rounds ORDER BY round_id DESC");

    if (!query.exec()) {
        emit errorOccurred("Failed to query rounds: " + query.lastError().text());
        return rounds;
    }

    while (query.next()) {
        RoundInfo info;
        info.roundId = query.value(0).toInt();
        info.startTimeUs = query.value(1).toLongLong();
        info.endTimeUs = query.value(2).toLongLong();
        info.status = query.value(3).toString();
        info.operatorName = query.value(4).toString();
        info.note = query.value(5).toString();
        rounds.append(info);
    }

    return rounds;
}

QList<qint64> DataQuerier::getWindowTimestamps(int roundId)
{
    QList<qint64> timestamps;

    if (!m_isInitialized) {
        return timestamps;
    }

    QSqlQuery query(m_db);
    query.prepare("SELECT window_start_us FROM time_windows "
                  "WHERE round_id = ? ORDER BY window_start_us");
    query.addBindValue(roundId);

    if (!query.exec()) {
        emit errorOccurred("Failed to query windows: " + query.lastError().text());
        return timestamps;
    }

    while (query.next()) {
        timestamps.append(query.value(0).toLongLong());
    }

    return timestamps;
}

DataQuerier::WindowData DataQuerier::getWindowData(int roundId, qint64 windowStartUs)
{
    WindowData data;
    data.windowStartUs = windowStartUs;

    if (!m_isInitialized) {
        return data;
    }

    // 1. 获取窗口ID
    QSqlQuery queryWindow(m_db);
    queryWindow.prepare("SELECT window_id FROM time_windows "
                        "WHERE round_id = ? AND window_start_us = ?");
    queryWindow.addBindValue(roundId);
    queryWindow.addBindValue(windowStartUs);

    if (!queryWindow.exec() || !queryWindow.next()) {
        return data;  // 窗口不存在
    }

    int windowId = queryWindow.value(0).toInt();

    // 2. 查询振动数据（解析BLOB）
    QSqlQuery queryVib(m_db);
    queryVib.prepare("SELECT channel_id, n_samples, data_blob "
                     "FROM vibration_blocks "
                     "WHERE window_id = ?");
    queryVib.addBindValue(windowId);

    if (queryVib.exec()) {
        while (queryVib.next()) {
            int channelId = queryVib.value(0).toInt();
            int nSamples = queryVib.value(1).toInt();
            QByteArray blob = queryVib.value(2).toByteArray();

            // 解析BLOB为float数组
            const float *floatData = reinterpret_cast<const float*>(blob.constData());
            QVector<float> values;
            values.reserve(nSamples);

            for (int i = 0; i < nSamples; ++i) {
                values.append(floatData[i]);
            }

            data.vibrationData[channelId] = values;
        }
    }

    // 3. 查询标量数据（包含channel_id用于区分不同电机）
    QSqlQuery queryScalar(m_db);
    queryScalar.prepare("SELECT sensor_type, channel_id, value "
                        "FROM scalar_samples "
                        "WHERE window_id = ? "
                        "ORDER BY timestamp_us");
    queryScalar.addBindValue(windowId);

    if (queryScalar.exec()) {
        while (queryScalar.next()) {
            int sensorType = queryScalar.value(0).toInt();
            int channelId = queryScalar.value(1).toInt();
            double value = queryScalar.value(2).toDouble();

            // 对于电机数据(300-303)，使用组合键区分不同电机
            // 组合键 = sensorType * 100 + channelId
            // 例如：电机2的位置(300) = 30002
            int key = sensorType;
            if (sensorType >= 300 && sensorType < 400) {
                key = sensorType * 100 + channelId;
            }

            data.scalarData[key].append(value);
        }
    }

    return data;
}

QList<DataQuerier::WindowData> DataQuerier::getTimeRangeData(int roundId,
                                                               qint64 startTimeUs,
                                                               qint64 endTimeUs)
{
    QList<WindowData> dataList;

    if (!m_isInitialized) {
        return dataList;
    }

    // 获取时间范围内的所有窗口时间戳
    QSqlQuery query(m_db);
    query.prepare("SELECT window_start_us FROM time_windows "
                  "WHERE round_id = ? AND window_start_us >= ? AND window_start_us < ? "
                  "ORDER BY window_start_us");
    query.addBindValue(roundId);
    query.addBindValue(startTimeUs);
    query.addBindValue(endTimeUs);

    if (!query.exec()) {
        emit errorOccurred("Failed to query time range: " + query.lastError().text());
        return dataList;
    }

    // 逐窗口查询数据
    while (query.next()) {
        qint64 windowStart = query.value(0).toLongLong();
        WindowData data = getWindowData(roundId, windowStart);
        dataList.append(data);
    }

    return dataList;
}

QList<DataQuerier::VibrationStats> DataQuerier::getVibrationStats(int roundId,
                                                                   int channelId,
                                                                   qint64 startTimeUs,
                                                                   qint64 endTimeUs)
{
    QList<VibrationStats> statsList;

    if (!m_isInitialized) {
        return statsList;
    }

    // 直接读取预计算的统计值（不解析BLOB，效率高）
    QSqlQuery query(m_db);
    query.prepare("SELECT start_ts_us, min_value, max_value, mean_value, rms_value "
                  "FROM vibration_blocks "
                  "WHERE round_id = ? AND channel_id = ? "
                  "AND start_ts_us >= ? AND start_ts_us < ? "
                  "ORDER BY start_ts_us");
    query.addBindValue(roundId);
    query.addBindValue(channelId);
    query.addBindValue(startTimeUs);
    query.addBindValue(endTimeUs);

    if (!query.exec()) {
        emit errorOccurred("Failed to query vibration stats: " + query.lastError().text());
        return statsList;
    }

    while (query.next()) {
        VibrationStats stats;
        stats.timestampUs = query.value(0).toLongLong();
        stats.minValue = query.value(1).toFloat();
        stats.maxValue = query.value(2).toFloat();
        stats.meanValue = query.value(3).toFloat();
        stats.rmsValue = query.value(4).toFloat();
        statsList.append(stats);
    }

    return statsList;
}

qint64 DataQuerier::getRoundActualDuration(int roundId)
{
    if (!m_isInitialized) {
        return 0;
    }

    // 从time_windows表查询实际的数据时间范围
    QSqlQuery query(m_db);
    query.prepare("SELECT MIN(window_start_us), MAX(window_end_us) "
                  "FROM time_windows WHERE round_id = ?");
    query.addBindValue(roundId);

    if (!query.exec() || !query.next()) {
        return 0;
    }

    qint64 minTime = query.value(0).toLongLong();
    qint64 maxTime = query.value(1).toLongLong();

    if (minTime == 0 || maxTime == 0) {
        return 0;
    }

    return (maxTime - minTime) / 1000000;  // 转换为秒
}
