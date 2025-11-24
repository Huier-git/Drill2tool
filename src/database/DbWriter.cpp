#include "database/DbWriter.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QThread>

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
    
    // 创建rounds表
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS rounds ("
        "round_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "start_ts_us INTEGER NOT NULL, "
        "end_ts_us INTEGER, "
        "operator_name TEXT, "
        "note TEXT, "
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP)")) {
        emit errorOccurred("Failed to create rounds table: " + query.lastError().text());
        return false;
    }
    
    // 创建scalar_samples表
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS scalar_samples ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "round_id INTEGER NOT NULL, "
        "sensor_type INTEGER NOT NULL, "
        "channel_id INTEGER NOT NULL, "
        "timestamp_us INTEGER NOT NULL, "
        "value REAL NOT NULL, "
        "seq_in_round INTEGER)")) {
        emit errorOccurred("Failed to create scalar_samples table: " + query.lastError().text());
        return false;
    }
    
    // 创建vibration_blocks表
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS vibration_blocks ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "round_id INTEGER NOT NULL, "
        "channel_id INTEGER NOT NULL, "
        "start_ts_us INTEGER NOT NULL, "
        "sample_rate REAL NOT NULL, "
        "n_samples INTEGER NOT NULL, "
        "data_blob BLOB NOT NULL, "
        "seq_in_round INTEGER)")) {
        emit errorOccurred("Failed to create vibration_blocks table: " + query.lastError().text());
        return false;
    }
    
    // 创建frequency_log表
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS frequency_log ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "round_id INTEGER, "
        "sensor_type INTEGER NOT NULL, "
        "old_freq REAL, "
        "new_freq REAL NOT NULL, "
        "timestamp_us INTEGER NOT NULL, "
        "comment TEXT)")) {
        emit errorOccurred("Failed to create frequency_log table: " + query.lastError().text());
        return false;
    }
    
    qDebug() << "Tables created manually";
    return true;
}

bool DbWriter::writeScalarData(const DataBlock &block)
{
    // 低频标量数据（MDB传感器、电机参数等）
    // 每个值作为一条记录写入
    
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO scalar_samples "
                  "(round_id, sensor_type, channel_id, timestamp_us, value, seq_in_round) "
                  "VALUES (:round_id, :sensor_type, :channel_id, :timestamp_us, :value, :seq)");
    
    for (int i = 0; i < block.values.size(); ++i) {
        // 计算每个样本的时间戳
        qint64 sampleTimestamp = block.startTimestampUs;
        if (block.sampleRate > 0 && i > 0) {
            sampleTimestamp += static_cast<qint64>((i * 1000000.0) / block.sampleRate);
        }
        
        query.bindValue(":round_id", block.roundId);
        query.bindValue(":sensor_type", static_cast<int>(block.sensorType));
        query.bindValue(":channel_id", block.channelId);
        query.bindValue(":timestamp_us", sampleTimestamp);
        query.bindValue(":value", block.values[i]);
        query.bindValue(":seq", block.numSamples > 0 ? i : QVariant());
        
        if (!query.exec()) {
            qWarning() << "Failed to write scalar data:" << query.lastError().text();
            return false;
        }
    }
    
    return true;
}

bool DbWriter::writeVibrationData(const DataBlock &block)
{
    // 高频振动数据 - 整块写入
    
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO vibration_blocks "
                  "(round_id, channel_id, start_ts_us, sample_rate, n_samples, data_blob, seq_in_round) "
                  "VALUES (:round_id, :channel_id, :start_ts, :sample_rate, :n_samples, :data_blob, :seq)");
    
    query.bindValue(":round_id", block.roundId);
    query.bindValue(":channel_id", block.channelId);
    query.bindValue(":start_ts", block.startTimestampUs);
    query.bindValue(":sample_rate", block.sampleRate);
    query.bindValue(":n_samples", block.numSamples);
    query.bindValue(":data_blob", block.blobData);
    query.bindValue(":seq", QVariant());
    
    if (!query.exec()) {
        qWarning() << "Failed to write vibration data:" << query.lastError().text();
        return false;
    }
    
    return true;
}

qint64 DbWriter::getCurrentTimestampUs()
{
    // 返回当前时间的微秒级时间戳
    return QDateTime::currentMSecsSinceEpoch() * 1000;
}
