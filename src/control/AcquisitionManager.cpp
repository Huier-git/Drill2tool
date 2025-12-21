#include "Logger.h"
#include "control/AcquisitionManager.h"
#include "dataACQ/VibrationWorker.h"
#include "dataACQ/MdbWorker.h"
#include "dataACQ/MotorWorker.h"
#include "database/DbWriter.h"
#include <QDebug>
#include <QDateTime>

AcquisitionManager::AcquisitionManager(QObject *parent)
    : QObject(parent)
    , m_vibrationWorker(nullptr)
    , m_mdbWorker(nullptr)
    , m_motorWorker(nullptr)
    , m_dbWriter(nullptr)
    , m_vibrationThread(nullptr)
    , m_mdbThread(nullptr)
    , m_motorThread(nullptr)
    , m_dbThread(nullptr)
    , m_currentRoundId(0)
    , m_isRunning(false)
    , m_isInitialized(false)
{
    LOG_DEBUG("AcquisitionManager", "Created");
}

AcquisitionManager::~AcquisitionManager()
{
    shutdown();
}

bool AcquisitionManager::initialize(const QString &dbPath)
{
    LOG_DEBUG("AcquisitionManager", "Initializing...");
    LOG_DEBUG_STREAM("AcquisitionManager") << "  Database path:" << dbPath;

    if (m_isInitialized) {
        LOG_WARNING("AcquisitionManager", "Already initialized");
        return true;
    }

    m_dbPath = dbPath;

    // 创建Worker实例
    setupWorkers();

    // 创建并启动线程
    setupThreads();

    // 连接信号
    connectSignals();

    m_isInitialized = true;
    LOG_DEBUG("AcquisitionManager", "Initialization complete");
    return true;
}

void AcquisitionManager::shutdown()
{
    if (!m_isInitialized) {
        return;
    }

    LOG_DEBUG("AcquisitionManager", "Shutting down...");

    // 停止所有采集
    if (m_isRunning) {
        stopAll();
    }

    // 清理线程
    cleanupThreads();

    m_isInitialized = false;
    LOG_DEBUG("AcquisitionManager", "Shutdown complete");
}

void AcquisitionManager::setupWorkers()
{
    LOG_DEBUG("AcquisitionManager", "Creating workers...");

    // 创建Worker实例（在主线程中创建）
    m_vibrationWorker = new VibrationWorker();
    m_mdbWorker = new MdbWorker();
    m_motorWorker = new MotorWorker();
    m_dbWriter = new DbWriter(m_dbPath);

    LOG_DEBUG("AcquisitionManager", "Workers created");
}

void AcquisitionManager::setupThreads()
{
    LOG_DEBUG("AcquisitionManager", "Setting up threads...");

    // 创建线程
    m_vibrationThread = new QThread(this);
    m_mdbThread = new QThread(this);
    m_motorThread = new QThread(this);
    m_dbThread = new QThread(this);

    // 设置线程名称（方便调试）
    m_vibrationThread->setObjectName("VibrationThread");
    m_mdbThread->setObjectName("MdbThread");
    m_motorThread->setObjectName("MotorThread");
    m_dbThread->setObjectName("DbThread");

    // 将Worker移动到对应线程
    m_vibrationWorker->moveToThread(m_vibrationThread);
    m_mdbWorker->moveToThread(m_mdbThread);
    m_motorWorker->moveToThread(m_motorThread);
    m_dbWriter->moveToThread(m_dbThread);

    // 启动线程
    m_vibrationThread->start();
    m_mdbThread->start();
    m_motorThread->start();
    m_dbThread->start();

    // 初始化DbWriter（必须在目标线程中初始化数据库连接）
    QMetaObject::invokeMethod(m_dbWriter, "initialize", Qt::BlockingQueuedConnection);

    LOG_DEBUG("AcquisitionManager", "Threads started");
}

void AcquisitionManager::connectSignals()
{
    LOG_DEBUG("AcquisitionManager", "Connecting signals...");

    // 连接Worker的dataBlockReady信号到DbWriter
    connect(m_vibrationWorker, &BaseWorker::dataBlockReady,
            m_dbWriter, &DbWriter::enqueueDataBlock, Qt::QueuedConnection);
    connect(m_mdbWorker, &BaseWorker::dataBlockReady,
            m_dbWriter, &DbWriter::enqueueDataBlock, Qt::QueuedConnection);
    connect(m_motorWorker, &BaseWorker::dataBlockReady,
            m_dbWriter, &DbWriter::enqueueDataBlock, Qt::QueuedConnection);

    // 连接Worker的错误信号
    connect(m_vibrationWorker, &BaseWorker::errorOccurred, this,
            [this](const QString &error) {
                emit errorOccurred("VibrationWorker", error);
            });
    connect(m_mdbWorker, &BaseWorker::errorOccurred, this,
            [this](const QString &error) {
                emit errorOccurred("MdbWorker", error);
            });
    connect(m_motorWorker, &BaseWorker::errorOccurred, this,
            [this](const QString &error) {
                emit errorOccurred("MotorWorker", error);
            });

    // 连接DbWriter的错误信号
    connect(m_dbWriter, &DbWriter::errorOccurred, this,
            [this](const QString &error) {
                emit errorOccurred("DbWriter", error);
            });

    // 连接DbWriter的统计信号
    connect(m_dbWriter, &DbWriter::statisticsUpdated, this,
            [this](qint64 totalBlocks, int queueSize) {
                QString info = QString("DB: %1 blocks written, Queue: %2")
                               .arg(totalBlocks).arg(queueSize);
                emit statisticsUpdated(info);
            });

    LOG_DEBUG("AcquisitionManager", "Signals connected");
}

void AcquisitionManager::cleanupThreads()
{
    LOG_DEBUG("AcquisitionManager", "Cleaning up threads...");

    // 停止并等待线程结束
    auto stopThread = [](QThread *thread, BaseWorker *worker, const QString &name) {
        if (thread && thread->isRunning()) {
            LOG_DEBUG_STREAM("AcquisitionManager") << "  Stopping" << name << "thread...";
            if (worker) {
                QMetaObject::invokeMethod(worker, "stop", Qt::QueuedConnection);
            }
            thread->quit();
            if (!thread->wait(3000)) {
                LOG_WARNING_STREAM("AcquisitionManager") << "  Thread" << name << "did not stop in time, terminating...";
                thread->terminate();
                thread->wait();
            }
            LOG_DEBUG_STREAM("AcquisitionManager") << "  " << name << "thread stopped";
        }
    };

    // 先停止Worker线程
    stopThread(m_vibrationThread, m_vibrationWorker, "Vibration");
    stopThread(m_mdbThread, m_mdbWorker, "MDB");
    stopThread(m_motorThread, m_motorWorker, "Motor");

    // 最后停止DbWriter线程（确保所有数据写完）
    if (m_dbThread && m_dbThread->isRunning()) {
        LOG_DEBUG("AcquisitionManager", "  Stopping DbWriter thread...");
        QMetaObject::invokeMethod(m_dbWriter, "shutdown", Qt::QueuedConnection);
        m_dbThread->quit();
        if (!m_dbThread->wait(5000)) {
            LOG_WARNING("AcquisitionManager", "  DbWriter thread did not stop in time, terminating...");
            m_dbThread->terminate();
            m_dbThread->wait();
        }
        LOG_DEBUG("AcquisitionManager", "  DbWriter thread stopped");
    }

    // 删除Worker
    if (m_vibrationWorker) {
        m_vibrationWorker->deleteLater();
        m_vibrationWorker = nullptr;
    }
    if (m_mdbWorker) {
        m_mdbWorker->deleteLater();
        m_mdbWorker = nullptr;
    }
    if (m_motorWorker) {
        m_motorWorker->deleteLater();
        m_motorWorker = nullptr;
    }
    if (m_dbWriter) {
        m_dbWriter->deleteLater();
        m_dbWriter = nullptr;
    }

    LOG_DEBUG("AcquisitionManager", "Threads cleaned up");
}

void AcquisitionManager::startAll()
{
    LOG_DEBUG("AcquisitionManager", "Starting all acquisition...");

    if (!m_isInitialized) {
        LOG_WARNING("AcquisitionManager", "Not initialized");
        return;
    }

    if (m_isRunning) {
        LOG_WARNING("AcquisitionManager", "Already running");
        return;
    }

    // 如果还没有轮次，创建一个
    if (m_currentRoundId == 0) {
        startNewRound();
    }

    // 启动所有Worker
    QMetaObject::invokeMethod(m_vibrationWorker, "start", Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_mdbWorker, "start", Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_motorWorker, "start", Qt::QueuedConnection);

    m_isRunning = true;
    emit acquisitionStateChanged(true);

    LOG_DEBUG("AcquisitionManager", "All acquisition started");
}

void AcquisitionManager::stopAll()
{
    LOG_DEBUG("AcquisitionManager", "Stopping all acquisition...");

    if (!m_isRunning) {
        LOG_WARNING("AcquisitionManager", "Not running");
        return;
    }

    // 停止所有Worker
    QMetaObject::invokeMethod(m_vibrationWorker, "stop", Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_mdbWorker, "stop", Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_motorWorker, "stop", Qt::QueuedConnection);

    m_isRunning = false;
    emit acquisitionStateChanged(false);

    LOG_DEBUG("AcquisitionManager", "All acquisition stopped");
}

void AcquisitionManager::startVibration()
{
    LOG_DEBUG("AcquisitionManager", "Starting vibration worker...");
    QMetaObject::invokeMethod(m_vibrationWorker, "start", Qt::QueuedConnection);
}

void AcquisitionManager::startMdb()
{
    LOG_DEBUG("AcquisitionManager", "Starting MDB worker...");
    QMetaObject::invokeMethod(m_mdbWorker, "start", Qt::QueuedConnection);
}

void AcquisitionManager::startMotor()
{
    LOG_DEBUG("AcquisitionManager", "Starting motor worker...");
    QMetaObject::invokeMethod(m_motorWorker, "start", Qt::QueuedConnection);
}

void AcquisitionManager::stopVibration()
{
    LOG_DEBUG("AcquisitionManager", "Stopping vibration worker...");
    QMetaObject::invokeMethod(m_vibrationWorker, "stop", Qt::QueuedConnection);
}

void AcquisitionManager::stopMdb()
{
    LOG_DEBUG("AcquisitionManager", "Stopping MDB worker...");
    QMetaObject::invokeMethod(m_mdbWorker, "stop", Qt::QueuedConnection);
}

void AcquisitionManager::stopMotor()
{
    LOG_DEBUG("AcquisitionManager", "Stopping motor worker...");
    QMetaObject::invokeMethod(m_motorWorker, "stop", Qt::QueuedConnection);
}

void AcquisitionManager::startNewRound(const QString &operatorName, const QString &note)
{
    LOG_DEBUG("AcquisitionManager", "Starting new round...");

    // 如果已经有活动的轮次（比如重置后），不创建新的
    if (m_currentRoundId > 0) {
        LOG_WARNING_STREAM("AcquisitionManager") << "Round already active, ID:" << m_currentRoundId;
        emit roundChanged(m_currentRoundId);
        return;
    }

    // 在DbWriter线程中创建新轮次
    int roundId = 0;
    QMetaObject::invokeMethod(m_dbWriter, "startNewRound",
                              Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(int, roundId),
                              Q_ARG(QString, operatorName),
                              Q_ARG(QString, note));

    m_currentRoundId = roundId;

    const qint64 baseTimestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
    QMetaObject::invokeMethod(m_vibrationWorker, "setTimeBase", Qt::QueuedConnection,
                              Q_ARG(qint64, baseTimestampUs));
    QMetaObject::invokeMethod(m_mdbWorker, "setTimeBase", Qt::QueuedConnection,
                              Q_ARG(qint64, baseTimestampUs));
    QMetaObject::invokeMethod(m_motorWorker, "setTimeBase", Qt::QueuedConnection,
                              Q_ARG(qint64, baseTimestampUs));

    // 通知所有Worker新的轮次ID
    QMetaObject::invokeMethod(m_vibrationWorker, "setRoundId", Qt::QueuedConnection, Q_ARG(int, roundId));
    QMetaObject::invokeMethod(m_mdbWorker, "setRoundId", Qt::QueuedConnection, Q_ARG(int, roundId));
    QMetaObject::invokeMethod(m_motorWorker, "setRoundId", Qt::QueuedConnection, Q_ARG(int, roundId));

    emit roundChanged(m_currentRoundId);

    LOG_DEBUG_STREAM("AcquisitionManager") << "New round started, ID:" << m_currentRoundId;
}

void AcquisitionManager::endCurrentRound()
{
    if (m_currentRoundId == 0) {
        LOG_WARNING("AcquisitionManager", "No active round to end");
        return;
    }

    LOG_DEBUG_STREAM("AcquisitionManager") << "Ending round" << m_currentRoundId;

    // 在DbWriter线程中结束轮次
    QMetaObject::invokeMethod(m_dbWriter, "endCurrentRound", Qt::QueuedConnection);

    int oldRoundId = m_currentRoundId;
    m_currentRoundId = 0;

    emit roundChanged(0);

    LOG_DEBUG_STREAM("AcquisitionManager") << "Round" << oldRoundId << "ended";
}

void AcquisitionManager::resetCurrentRound()
{
    if (m_currentRoundId == 0) {
        LOG_WARNING("AcquisitionManager", "No active round to reset");
        return;
    }

    LOG_DEBUG_STREAM("AcquisitionManager") << "Resetting round" << m_currentRoundId;

    // 清除当前轮次的所有数据
    QMetaObject::invokeMethod(m_dbWriter, "clearRoundData", Qt::BlockingQueuedConnection,
                              Q_ARG(int, m_currentRoundId));

    // 重置时间基准
    const qint64 baseTimestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
    QMetaObject::invokeMethod(m_vibrationWorker, "setTimeBase", Qt::QueuedConnection,
                              Q_ARG(qint64, baseTimestampUs));
    QMetaObject::invokeMethod(m_mdbWorker, "setTimeBase", Qt::QueuedConnection,
                              Q_ARG(qint64, baseTimestampUs));
    QMetaObject::invokeMethod(m_motorWorker, "setTimeBase", Qt::QueuedConnection,
                              Q_ARG(qint64, baseTimestampUs));

    LOG_DEBUG_STREAM("AcquisitionManager") << "Round" << m_currentRoundId << "reset complete";
}

