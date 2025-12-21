#include "dataACQ/BaseWorker.h"
#include <QDebug>
#include <QDateTime>

BaseWorker::BaseWorker(QObject *parent)
    : QObject(parent)
    , m_state(WorkerState::Stopped)
    , m_currentRoundId(0)
    , m_sampleRate(0.0)
    , m_samplesCollected(0)
    , m_stopRequested(false)
    , m_timeBaseUs(0)
    , m_hasTimeBase(false)
{
    // 注册元类型，使其可以在跨线程信号槽中使用
    qRegisterMetaType<DataBlock>("DataBlock");
    qRegisterMetaType<SensorType>("SensorType");
    qRegisterMetaType<WorkerState>("WorkerState");

    m_elapsedTimer.invalidate();
}

BaseWorker::~BaseWorker()
{
    if (m_state == WorkerState::Running) {
        stop();
    }
}

void BaseWorker::start()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_state == WorkerState::Running) {
        qWarning() << "Worker already running";
        return;
    }
    
    setState(WorkerState::Starting);
    m_stopRequested = false;
    m_samplesCollected = 0;
    
    locker.unlock();
    
    // 初始化硬件
    if (!initializeHardware()) {
        emitError("Failed to initialize hardware");
        setState(WorkerState::Error);
        return;
    }
    
    setState(WorkerState::Running);
    
    // 启动采集循环（子类实现）
    runAcquisition();
}

void BaseWorker::stop()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_state == WorkerState::Stopped) {
        return;
    }
    
    setState(WorkerState::Stopping);
    m_stopRequested = true;
    
    locker.unlock();
    
    // 关闭硬件
    shutdownHardware();
    
    setState(WorkerState::Stopped);
    
    qDebug() << "Worker stopped, total samples collected:" << m_samplesCollected;
}

void BaseWorker::pause()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_state != WorkerState::Running) {
        return;
    }
    
    setState(WorkerState::Paused);
}

void BaseWorker::resume()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_state != WorkerState::Paused) {
        return;
    }
    
    setState(WorkerState::Running);
}

void BaseWorker::setRoundId(int roundId)
{
    QMutexLocker locker(&m_mutex);
    m_currentRoundId = roundId;
    qDebug() << "Worker round ID set to:" << roundId;
}

void BaseWorker::setSampleRate(double rate)
{
    QMutexLocker locker(&m_mutex);
    m_sampleRate = rate;
    qDebug() << "Worker sample rate set to:" << rate << "Hz";
}

void BaseWorker::setTimeBase(qint64 baseTimestampUs)
{
    QMutexLocker locker(&m_mutex);
    m_timeBaseUs = baseTimestampUs;
    m_elapsedTimer.restart();
    m_hasTimeBase = true;
}

void BaseWorker::setState(WorkerState newState)
{
    QMutexLocker locker(&m_mutex);
    if (m_state != newState) {
        m_state = newState;
        emit stateChanged(newState);
        qDebug() << "Worker state changed to:" << workerStateToString(newState);
    }
}

void BaseWorker::emitError(const QString &errorMsg)
{
    qCritical() << "Worker error:" << errorMsg;
    emit errorOccurred(errorMsg);
}

bool BaseWorker::shouldContinue() const
{
    QMutexLocker locker(&m_mutex);
    return !m_stopRequested && m_state == WorkerState::Running;
}

qint64 BaseWorker::currentTimestampUs() const
{
    QMutexLocker locker(&m_mutex);
    if (m_hasTimeBase && m_elapsedTimer.isValid()) {
        return m_timeBaseUs + (m_elapsedTimer.nsecsElapsed() / 1000);
    }
    return QDateTime::currentMSecsSinceEpoch() * 1000;
}
