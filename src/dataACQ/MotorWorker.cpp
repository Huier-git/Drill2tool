#include "dataACQ/MotorWorker.h"
#include "Global.h"
#include "control/zmotion.h"
#include "control/zmcaux.h"
#include <QDebug>
#include <QDateTime>
#include <QThread>
#include <QMutexLocker>

MotorWorker::MotorWorker(QObject *parent)
    : BaseWorker(parent)
    , m_controllerAddress("192.168.0.11")
    , m_readTimer(nullptr)
    , m_readPosition(true)
    , m_readSpeed(true)
    , m_readTorque(true)
    , m_readCurrent(true)
    , m_sampleCount(0)
{
    m_sampleRate = 100.0;  // 默认100Hz
    m_motorIds = {0, 1, 2, 3, 4, 5, 6, 7};  // 默认8个电机
    qDebug() << "[MotorWorker] Created. Default: 100Hz, 8 motors (uses global g_handle)";
}

MotorWorker::~MotorWorker()
{
    if (m_readTimer) {
        m_readTimer->stop();
        delete m_readTimer;
        m_readTimer = nullptr;
    }
}

void MotorWorker::setReadParameters(bool pos, bool speed, bool torque, bool current)
{
    m_readPosition = pos;
    m_readSpeed = speed;
    m_readTorque = torque;
    m_readCurrent = current;

    qDebug() << "[MotorWorker] Read parameters set:"
             << "Pos=" << pos << "Speed=" << speed
             << "Torque=" << torque << "Current=" << current;
}

bool MotorWorker::isConnected() const
{
    QMutexLocker locker(&g_mutex);
    return g_handle != nullptr;
}

bool MotorWorker::initializeHardware()
{
    qDebug() << "[MotorWorker] Initializing (using global g_handle)...";
    qDebug() << "  Sample Rate:" << m_sampleRate << "Hz";
    qDebug() << "  Motor IDs:" << m_motorIds;

    // 检查全局句柄是否已连接
    if (!isConnected()) {
        qWarning() << "[MotorWorker] Global g_handle not connected, data acquisition will wait...";
        // 不返回 false，允许启动但不采集数据
    }

    // 创建定时器
    m_readTimer = new QTimer(this);
    int intervalMs = static_cast<int>(1000.0 / m_sampleRate);
    m_readTimer->setInterval(intervalMs);
    connect(m_readTimer, &QTimer::timeout, this, &MotorWorker::readMotorParameters);

    qDebug() << "[MotorWorker] Hardware initialized, read interval:" << intervalMs << "ms";
    return true;
}

void MotorWorker::shutdownHardware()
{
    qDebug() << "[MotorWorker] Shutting down...";

    // 停止定时器
    if (m_readTimer) {
        m_readTimer->stop();
        delete m_readTimer;
        m_readTimer = nullptr;
    }

    // 不断开连接，连接由 ZMotionDriver 统一管理

    qDebug() << "[MotorWorker] Shutdown complete. Total samples:" << m_sampleCount;
}

void MotorWorker::runAcquisition()
{
    qDebug() << "[MotorWorker] Starting acquisition timer...";

    // 启动定时器
    m_readTimer->start();

    // 进入事件循环
    while (shouldContinue()) {
        // 处理事件（让定时器能够触发）
        QThread::msleep(50);

        // 每10秒输出统计
        if (m_sampleCount > 0 && m_sampleCount % 1000 == 0) {
            emit statisticsUpdated(m_samplesCollected, m_sampleRate);
        }
    }

    // 停止定时器
    if (m_readTimer) {
        m_readTimer->stop();
    }

    qDebug() << "[MotorWorker] Acquisition loop ended";
}

void MotorWorker::readMotorParameters()
{
    if (!shouldContinue()) {
        return;
    }

    // 检查全局句柄
    if (!isConnected()) {
        return;  // 静默跳过，不输出警告（避免日志刷屏）
    }

    // 遍历所有电机
    for (int motorId : m_motorIds) {
        // 读取位置
        if (m_readPosition) {
            double position;
            if (readMotorPosition(motorId, position)) {
                sendDataBlock(motorId, SensorType::Motor_Position, position);
            }
        }

        // 读取速度
        if (m_readSpeed) {
            double speed;
            if (readMotorSpeed(motorId, speed)) {
                sendDataBlock(motorId, SensorType::Motor_Speed, speed);
            }
        }

        // 读取扭矩
        if (m_readTorque) {
            double torque;
            if (readMotorTorque(motorId, torque)) {
                sendDataBlock(motorId, SensorType::Motor_Torque, torque);
            }
        }

        // 读取电流
        if (m_readCurrent) {
            double current;
            if (readMotorCurrent(motorId, current)) {
                sendDataBlock(motorId, SensorType::Motor_Current, current);
            }
        }
    }

    m_sampleCount++;
    int paramsPerMotor = (m_readPosition ? 1 : 0) + (m_readSpeed ? 1 : 0) +
                         (m_readTorque ? 1 : 0) + (m_readCurrent ? 1 : 0);
    m_samplesCollected += m_motorIds.size() * paramsPerMotor;
}

bool MotorWorker::readMotorPosition(int motorId, double &position)
{
    QMutexLocker locker(&g_mutex);
    if (!g_handle) return false;

    float pos = 0.0f;
    // 使用反馈位置 MPOS
    int32 ret = ZAux_Direct_GetMpos(g_handle, motorId, &pos);
    if (ret == ERR_OK) {
        position = static_cast<double>(pos);
        return true;
    }
    return false;
}

bool MotorWorker::readMotorSpeed(int motorId, double &speed)
{
    QMutexLocker locker(&g_mutex);
    if (!g_handle) return false;

    float spd = 0.0f;
    // 使用反馈速度 MSPEED
    int32 ret = ZAux_Direct_GetMspeed(g_handle, motorId, &spd);
    if (ret == ERR_OK) {
        speed = static_cast<double>(spd);
        return true;
    }
    return false;
}

bool MotorWorker::readMotorTorque(int motorId, double &torque)
{
    QMutexLocker locker(&g_mutex);
    if (!g_handle) return false;

    float trq = 0.0f;
    // 尝试读取 "TORQUE"
    int32 ret = ZAux_Direct_GetParam(g_handle, "TORQUE", motorId, &trq);
    if (ret == ERR_OK) {
        torque = static_cast<double>(trq);
        return true;
    }
    return false;
}

bool MotorWorker::readMotorCurrent(int motorId, double &current)
{
    QMutexLocker locker(&g_mutex);
    if (!g_handle) return false;

    float cur = 0.0f;
    // 尝试读取 "CURRENT" (或 "RL_CURRENT")
    int32 ret = ZAux_Direct_GetParam(g_handle, "CURRENT", motorId, &cur);
    if (ret == ERR_OK) {
        current = static_cast<double>(cur);
        return true;
    }
    return false;
}

void MotorWorker::sendDataBlock(int motorId, SensorType type, double value)
{
    DataBlock block;
    block.roundId = m_currentRoundId;
    block.sensorType = type;
    block.channelId = motorId;  // 通道ID = 电机ID
    block.startTimestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
    block.sampleRate = m_sampleRate;
    block.numSamples = 1;
    block.values.append(value);

    emit dataBlockReady(block);
}
