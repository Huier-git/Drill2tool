#include "dataACQ/MotorWorker.h"
#include "Logger.h"
#include "Global.h"
#include "control/zmotion.h"
#include "control/zmcaux.h"
#include <QDebug>
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
    m_sampleRate = 10.0;  // 默认10Hz（电机参数刷新无需太快）
    m_motorIds = {0, 1, 2, 3, 4, 5, 6, 7};  // 默认8个电机
    LOG_DEBUG("MotorWorker", "Created. Default: 10Hz, 8 motors (uses global g_handle)");
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

    LOG_DEBUG_STREAM("MotorWorker")
        << "Read parameters set: Pos=" << pos << "Speed=" << speed
        << "Torque=" << torque << "Current=" << current;
}

bool MotorWorker::isConnected() const
{
    QMutexLocker locker(&g_mutex);
    return g_handle != nullptr;
}

bool MotorWorker::initializeHardware()
{
    LOG_DEBUG("MotorWorker", "Initializing (using global g_handle)...");
    LOG_DEBUG_STREAM("MotorWorker") << "  Sample Rate:" << m_sampleRate << "Hz";
    LOG_DEBUG_STREAM("MotorWorker") << "  Motor IDs:" << m_motorIds;

    // 检查全局句柄是否已连接
    if (!isConnected()) {
        LOG_WARNING("MotorWorker", "Global g_handle not connected, data acquisition will wait...");
        // 不返回 false，允许启动但不采集数据
    }

    // 创建定时器
    m_readTimer = new QTimer(this);
    int intervalMs = static_cast<int>(1000.0 / m_sampleRate);
    m_readTimer->setInterval(intervalMs);
    connect(m_readTimer, &QTimer::timeout, this, &MotorWorker::readMotorParameters);

    LOG_DEBUG_STREAM("MotorWorker") << "Hardware initialized, read interval:" << intervalMs << "ms";
    return true;
}

void MotorWorker::shutdownHardware()
{
    LOG_DEBUG("MotorWorker", "Shutting down...");

    // 停止定时器
    if (m_readTimer) {
        m_readTimer->stop();
        delete m_readTimer;
        m_readTimer = nullptr;
    }

    // 不断开连接，连接由 ZMotionDriver 统一管理

    LOG_DEBUG_STREAM("MotorWorker") << "Shutdown complete. Total samples:" << m_sampleCount;
}

void MotorWorker::runAcquisition()
{
    LOG_DEBUG("MotorWorker", "Starting acquisition timer...");

    if (!m_readTimer) {
        emitError("Read timer not initialized");
        setState(WorkerState::Error);
        return;
    }

    // Start timer and return so the thread event loop can deliver timeouts.
    m_readTimer->start();
    LOG_DEBUG("MotorWorker", "Acquisition timer started");
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

    if (m_sampleCount > 0 && m_sampleCount % 1000 == 0) {
        emit statisticsUpdated(m_samplesCollected, m_sampleRate);
    }
}

bool MotorWorker::readMotorPosition(int motorId, double &position)
{
    QMutexLocker locker(&g_mutex);
    if (!g_handle) return false;

    // 使用MotorMap映射实际的电机轴号
    int mappedAxis = MotorMap[motorId];

    float pos = 0.0f;
    // 使用反馈位置 MPOS
    int32 ret = ZAux_Direct_GetMpos(g_handle, mappedAxis, &pos);
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

    // 使用MotorMap映射实际的电机轴号
    int mappedAxis = MotorMap[motorId];

    float spd = 0.0f;
    // 使用反馈速度 MSPEED
    int32 ret = ZAux_Direct_GetMspeed(g_handle, mappedAxis, &spd);
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

    // 使用MotorMap映射实际的电机轴号
    int mappedAxis = MotorMap[motorId];

    float trq = 0.0f;
    // 使用 DRIVE_TORQUE（Linux版本使用的参数名）
    int32 ret = ZAux_Direct_GetParam(g_handle, "DRIVE_TORQUE", mappedAxis, &trq);
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

    // 使用MotorMap映射实际的电机轴号
    int mappedAxis = MotorMap[motorId];

    float dac = 0.0f;
    // 读取 DAC 值作为电流（Linux版本的做法）
    int32 ret = ZAux_Direct_GetDAC(g_handle, mappedAxis, &dac);
    if (ret == ERR_OK) {
        current = static_cast<double>(dac);
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
    block.startTimestampUs = currentTimestampUs();
    block.sampleRate = m_sampleRate;
    block.numSamples = 1;
    block.values.append(value);

    emit dataBlockReady(block);
}
