#include "database/DbWriter.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QThread>
#include <QtMath>
#include <cfloat>

DbWriter::DbWriter(const QString &dbPath, QObject *parent)
    : QObject(parent)
    , m_dbPath(dbPath)
    , m_batchTimer(nullptr)
    , m_currentRoundId(0)
    , m_maxQueueSize(10000)
    , m_batchSize(200)
    , m_batchIntervalMs(100)
    , m_totalBlocksWritten(0)
    , m_isInitialized(false)
    , m_maxCacheSize(100)  // 窗口缓存大小
{
    qDebug() << "DbWriter created, db path:" << m_dbPath;
}

DbWriter::~DbWriter()
{
    shutdown();
}

bool DbWriter::initialize()
{
    qDebug() << "DbWriter initializing...";
    
    if (m_isInitialized) {
        qWarning() << "DbWriter already initialized";
        return true;
    }
    
    // 初始化数据库连接
    if (!initializeDatabase()) {
        return false;
    }
    
    // 创建批量写入定时器
    m_batchTimer = new QTimer(this);
    m_batchTimer->setInterval(m_batchIntervalMs);
    connect(m_batchTimer, &QTimer::timeout, this, &DbWriter::processBatch);
    m_batchTimer->start();
    
    m_isInitialized = true;
    qDebug() << "DbWriter initialized successfully";
    return true;
}

void DbWriter::shutdown()
{
    if (!m_isInitialized) {
        return;
    }

    qDebug() << "DbWriter shutting down...";

    // 停止定时器
    if (m_batchTimer) {
        m_batchTimer->stop();
        delete m_batchTimer;
        m_batchTimer = nullptr;
    }

    // 处理剩余队列
    processBatch();

    // 清理窗口缓存
    clearWindowCache();

    // 关闭数据库
    if (m_db.isOpen()) {
        m_db.close();
    }

    m_isInitialized = false;
    qDebug() << "DbWriter shutdown complete. Total blocks written:" << m_totalBlocksWritten;
}

int DbWriter::queueSize() const
{
    QMutexLocker locker(&m_queueMutex);
    return m_queue.size();
}

void DbWriter::enqueueDataBlock(const DataBlock &block)
{
    QMutexLocker locker(&m_queueMutex);
    
    // 检查队列是否过满
    if (m_queue.size() >= m_maxQueueSize) {
        locker.unlock();
        emit queueWarning(m_queue.size(), m_maxQueueSize);
        qWarning() << "Queue full! Dropping data block. SensorType:" 
                   << sensorTypeToString(block.sensorType);
        return;
    }
    
    m_queue.enqueue(block);
    
    // 如果队列达到批量大小，立即处理
    if (m_queue.size() >= m_batchSize) {
        locker.unlock();
        processBatch();
    }
}

void DbWriter::processBatch()
{
    QMutexLocker locker(&m_queueMutex);
    
    if (m_queue.isEmpty()) {
        return;
    }
    
    // 取出一批数据
    QVector<DataBlock> batch;
    int batchCount = qMin(m_batchSize, m_queue.size());
    batch.reserve(batchCount);
    
    for (int i = 0; i < batchCount; ++i) {
        batch.append(m_queue.dequeue());
    }
    
    locker.unlock();
    
    // 开始事务
    if (!m_db.transaction()) {
        emit errorOccurred("Failed to start transaction: " + m_db.lastError().text());
        return;
    }
    
    // 批量写入数据
    int successCount = 0;
    for (const DataBlock &block : batch) {
        bool success = false;
        
        // 根据传感器类型选择写入方法
        if (block.sensorType >= SensorType::Vibration_X && 
            block.sensorType <= SensorType::Vibration_Z) {
            // 高频振动数据
            success = writeVibrationData(block);
        } else {
            // 低频标量数据
            success = writeScalarData(block);
        }
        
        if (success) {
            successCount++;
        }
    }
    
    // 提交事务
    if (!m_db.commit()) {
        m_db.rollback();
        emit errorOccurred("Failed to commit transaction: " + m_db.lastError().text());
        return;
    }
    
    m_totalBlocksWritten += successCount;
    emit batchWritten(successCount);
    emit statisticsUpdated(m_totalBlocksWritten, queueSize());
}

int DbWriter::startNewRound(const QString &operatorName, const QString &note)
{
    if (!m_isInitialized) {
        qWarning() << "DbWriter not initialized";
        return -1;
    }
    
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO rounds (start_ts_us, operator_name, note) "
                  "VALUES (:start_ts, :operator, :note)");
    query.bindValue(":start_ts", getCurrentTimestampUs());
    query.bindValue(":operator", operatorName);
    query.bindValue(":note", note);
    
    if (!query.exec()) {
        emit errorOccurred("Failed to create new round: " + query.lastError().text());
        return -1;
    }
    
    m_currentRoundId = query.lastInsertId().toInt();
    qDebug() << "New round started, ID:" << m_currentRoundId;
    return m_currentRoundId;
}

void DbWriter::endCurrentRound()
{
    if (m_currentRoundId == 0) {
        qWarning() << "No active round to end";
        return;
    }
    
    QSqlQuery query(m_db);
    query.prepare("UPDATE rounds SET end_ts_us = :end_ts WHERE round_id = :round_id");
    query.bindValue(":end_ts", getCurrentTimestampUs());
    query.bindValue(":round_id", m_currentRoundId);
    
    if (!query.exec()) {
        emit errorOccurred("Failed to end round: " + query.lastError().text());
        return;
    }
    
    qDebug() << "Round ended, ID:" << m_currentRoundId;
    m_currentRoundId = 0;
}

void DbWriter::logFrequencyChange(int roundId, SensorType sensorType, 
                                   double oldFreq, double newFreq, 
                                   const QString &comment)
{
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO frequency_log "
                  "(round_id, sensor_type, old_freq, new_freq, timestamp_us, comment) "
                  "VALUES (:round_id, :sensor_type, :old_freq, :new_freq, :timestamp, :comment)");
    query.bindValue(":round_id", roundId);
    query.bindValue(":sensor_type", static_cast<int>(sensorType));
    query.bindValue(":old_freq", oldFreq);
    query.bindValue(":new_freq", newFreq);
    query.bindValue(":timestamp", getCurrentTimestampUs());
    query.bindValue(":comment", comment);
    
    if (!query.exec()) {
        emit errorOccurred("Failed to log frequency change: " + query.lastError().text());
    }
}

bool DbWriter::initializeDatabase()
{
    // 创建数据库连接（使用线程ID作为连接名）
    QString connectionName = QString("DbWriter_%1").arg((qint64)QThread::currentThreadId());
    m_db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    m_db.setDatabaseName(m_dbPath);
    
    if (!m_db.open()) {
        emit errorOccurred("Failed to open database: " + m_db.lastError().text());
        return false;
    }
    
    qDebug() << "Database opened:" << m_dbPath;
    
    // 创建表结构
    if (!createTables()) {
        return false;
    }
    
    return true;
}

bool DbWriter::createTables()
{
    // 读取schema.sql文件
    QFile schemaFile(":/database/schema.sql");
    if (!schemaFile.exists()) {
        // 如果资源文件不存在，尝试从文件系统读取
        schemaFile.setFileName("database/schema.sql");
    }
    
    if (!schemaFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Cannot open schema.sql, creating tables manually...";
        // 手动创建表（精简版）
        return createTablesManually();
    }
    
    QString sql = schemaFile.readAll();
    schemaFile.close();
    
    // 执行SQL脚本
    QStringList statements = sql.split(';', Qt::SkipEmptyParts);
    QSqlQuery query(m_db);
    
    for (const QString &statement : statements) {
        QString trimmed = statement.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith("--")) {
            continue;
        }
        
        if (!query.exec(trimmed)) {
            qWarning() << "Failed to execute SQL:" << query.lastError().text();
            qWarning() << "Statement:" << trimmed;
        }
    }
    
    qDebug() << "Database tables created/verified";
    return true;
}

bool DbWriter::createTablesManually()
{
    QSqlQuery query(m_db);

    // 创建rounds表（添加status字段）
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS rounds ("
        "round_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "start_ts_us INTEGER NOT NULL, "
        "end_ts_us INTEGER, "
        "status TEXT DEFAULT 'running', "
        "operator_name TEXT, "
        "note TEXT, "
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP)")) {
        emit errorOccurred("Failed to create rounds table: " + query.lastError().text());
        return false;
    }

    // 创建time_windows表（核心创新）
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS time_windows ("
        "window_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "round_id INTEGER NOT NULL, "
        "window_start_us INTEGER NOT NULL, "
        "window_end_us INTEGER NOT NULL, "
        "has_vibration INTEGER DEFAULT 0, "
        "has_mdb INTEGER DEFAULT 0, "
        "has_motor INTEGER DEFAULT 0)")) {
        emit errorOccurred("Failed to create time_windows table: " + query.lastError().text());
        return false;
    }

    // 创建time_windows索引
    query.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_tw_round_start "
               "ON time_windows(round_id, window_start_us)");

    // 创建scalar_samples表（添加window_id）
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS scalar_samples ("
        "sample_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "round_id INTEGER NOT NULL, "
        "window_id INTEGER NOT NULL, "
        "sensor_type INTEGER NOT NULL, "
        "channel_id INTEGER NOT NULL, "
        "timestamp_us INTEGER NOT NULL, "
        "value REAL NOT NULL)")) {
        emit errorOccurred("Failed to create scalar_samples table: " + query.lastError().text());
        return false;
    }

    // 创建scalar_samples索引
    query.exec("CREATE INDEX IF NOT EXISTS idx_scalar_window ON scalar_samples(window_id)");

    // 创建vibration_blocks表（添加window_id和统计字段）
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS vibration_blocks ("
        "block_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "round_id INTEGER NOT NULL, "
        "window_id INTEGER NOT NULL, "
        "channel_id INTEGER NOT NULL, "
        "start_ts_us INTEGER NOT NULL, "
        "sample_rate REAL NOT NULL, "
        "n_samples INTEGER NOT NULL, "
        "data_blob BLOB NOT NULL, "
        "min_value REAL, "
        "max_value REAL, "
        "mean_value REAL, "
        "rms_value REAL)")) {
        emit errorOccurred("Failed to create vibration_blocks table: " + query.lastError().text());
        return false;
    }

    // 创建vibration_blocks索引
    query.exec("CREATE INDEX IF NOT EXISTS idx_vib_window ON vibration_blocks(window_id)");

    // 创建events表
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS events ("
        "event_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "round_id INTEGER NOT NULL, "
        "window_id INTEGER, "
        "event_type TEXT NOT NULL, "
        "timestamp_us INTEGER NOT NULL, "
        "description TEXT)")) {
        emit errorOccurred("Failed to create events table: " + query.lastError().text());
        return false;
    }

    // 创建frequency_log表
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS frequency_log ("
        "log_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "round_id INTEGER, "
        "sensor_type INTEGER NOT NULL, "
        "old_freq REAL, "
        "new_freq REAL NOT NULL, "
        "timestamp_us INTEGER NOT NULL, "
        "comment TEXT)")) {
        emit errorOccurred("Failed to create frequency_log table: " + query.lastError().text());
        return false;
    }

    // 创建system_config表
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS system_config ("
        "key TEXT PRIMARY KEY, "
        "value TEXT NOT NULL, "
        "description TEXT, "
        "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP)")) {
        emit errorOccurred("Failed to create system_config table: " + query.lastError().text());
        return false;
    }

    // 插入默认配置
    query.exec("INSERT OR IGNORE INTO system_config (key, value, description) "
               "VALUES ('db_version', '2.0', '数据库版本')");
    query.exec("INSERT OR IGNORE INTO system_config (key, value, description) "
               "VALUES ('window_duration_us', '1000000', '时间窗口时长（微秒）')");

    qDebug() << "Database v2.0 tables created manually";
    return true;
}

bool DbWriter::writeScalarData(const DataBlock &block)
{
    // 获取或创建时间窗口
    int windowId = getOrCreateWindow(block.roundId, block.startTimestampUs);
    if (windowId < 0) {
        return false;
    }

    // 低频标量数据（MDB传感器、电机参数等）
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO scalar_samples "
                  "(round_id, window_id, sensor_type, channel_id, timestamp_us, value) "
                  "VALUES (?, ?, ?, ?, ?, ?)");

    for (int i = 0; i < block.values.size(); ++i) {
        // 计算每个样本的时间戳
        qint64 sampleTimestamp = block.startTimestampUs;
        if (block.sampleRate > 0 && i > 0) {
            sampleTimestamp += static_cast<qint64>((i * 1000000.0) / block.sampleRate);
        }

        query.addBindValue(block.roundId);
        query.addBindValue(windowId);
        query.addBindValue(static_cast<int>(block.sensorType));
        query.addBindValue(block.channelId);
        query.addBindValue(sampleTimestamp);
        query.addBindValue(block.values[i]);

        if (!query.exec()) {
            qWarning() << "Failed to write scalar data:" << query.lastError().text();
            return false;
        }
    }

    // 更新窗口状态
    if (block.sensorType >= SensorType::Force_Upper &&
        block.sensorType <= SensorType::Position_MDB) {
        updateWindowStatus(windowId, "mdb");
    } else if (block.sensorType >= SensorType::Motor_Position &&
               block.sensorType <= SensorType::Motor_Current) {
        updateWindowStatus(windowId, "motor");
    }

    return true;
}

bool DbWriter::writeVibrationData(const DataBlock &block)
{
    // 获取或创建时间窗口
    int windowId = getOrCreateWindow(block.roundId, block.startTimestampUs);
    if (windowId < 0) {
        return false;
    }

    // 预计算统计特征（KISS：简单遍历）
    float minVal = FLT_MAX;
    float maxVal = -FLT_MAX;
    double sum = 0.0;
    double sumSq = 0.0;

    const float *data = reinterpret_cast<const float*>(block.blobData.constData());
    int n = block.numSamples;

    for (int i = 0; i < n; ++i) {
        float val = data[i];
        if (val < minVal) minVal = val;
        if (val > maxVal) maxVal = val;
        sum += val;
        sumSq += val * val;
    }

    double mean = (n > 0) ? (sum / n) : 0.0;
    double rms = (n > 0) ? qSqrt(sumSq / n) : 0.0;

    // 写入数据库
    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO vibration_blocks "
        "(round_id, window_id, channel_id, start_ts_us, sample_rate, "
        "n_samples, data_blob, min_value, max_value, mean_value, rms_value) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );

    query.addBindValue(block.roundId);
    query.addBindValue(windowId);
    query.addBindValue(block.channelId);
    query.addBindValue(block.startTimestampUs);
    query.addBindValue(block.sampleRate);
    query.addBindValue(n);
    query.addBindValue(block.blobData);
    query.addBindValue(minVal);
    query.addBindValue(maxVal);
    query.addBindValue(mean);
    query.addBindValue(rms);

    if (!query.exec()) {
        qWarning() << "Failed to write vibration data:" << query.lastError().text();
        return false;
    }

    // 更新窗口状态
    updateWindowStatus(windowId, "vibration");

    return true;
}

qint64 DbWriter::getCurrentTimestampUs()
{
    // 返回当前时间的微秒级时间戳
    return QDateTime::currentMSecsSinceEpoch() * 1000;
}

// ============================================
// 时间窗口管理功能
// ============================================

int DbWriter::getOrCreateWindow(int roundId, qint64 timestampUs)
{
    // 计算窗口起始时间（向下取整到秒边界）
    qint64 windowStart = (timestampUs / 1000000) * 1000000;
    qint64 windowEnd = windowStart + 1000000;

    // 先查缓存
    if (m_windowCache.contains(windowStart)) {
        return m_windowCache[windowStart];
    }

    // 查询数据库
    QSqlQuery query(m_db);
    query.prepare("SELECT window_id FROM time_windows "
                  "WHERE round_id = ? AND window_start_us = ?");
    query.addBindValue(roundId);
    query.addBindValue(windowStart);

    if (query.exec() && query.next()) {
        int windowId = query.value(0).toInt();
        m_windowCache[windowStart] = windowId;
        return windowId;
    }

    // 创建新窗口
    query.prepare("INSERT INTO time_windows "
                  "(round_id, window_start_us, window_end_us) "
                  "VALUES (?, ?, ?)");
    query.addBindValue(roundId);
    query.addBindValue(windowStart);
    query.addBindValue(windowEnd);

    if (!query.exec()) {
        qWarning() << "Failed to create window:" << query.lastError().text();
        return -1;
    }

    int windowId = query.lastInsertId().toInt();
    m_windowCache[windowStart] = windowId;

    // 缓存大小控制
    if (m_windowCache.size() > m_maxCacheSize) {
        // 移除最旧的条目
        m_windowCache.erase(m_windowCache.begin());
    }

    return windowId;
}

void DbWriter::updateWindowStatus(int windowId, const QString &dataType)
{
    // 简洁实现：只更新对应的标志
    QString field;
    if (dataType == "vibration") {
        field = "has_vibration";
    } else if (dataType == "mdb") {
        field = "has_mdb";
    } else if (dataType == "motor") {
        field = "has_motor";
    } else {
        return;
    }

    QSqlQuery query(m_db);
    QString sql = QString("UPDATE time_windows SET %1 = 1 WHERE window_id = ?").arg(field);
    query.prepare(sql);
    query.addBindValue(windowId);

    if (!query.exec()) {
        qWarning() << "Failed to update window status:" << query.lastError().text();
    }
}

void DbWriter::clearWindowCache()
{
    m_windowCache.clear();
}
