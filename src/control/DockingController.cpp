#include "control/DockingController.h"
#include <QDebug>
#include <QThread>
#include <QEventLoop>

// ============================================================================
// DockingConfig 实现
// ============================================================================

DockingConfig DockingConfig::fromJson(const QJsonObject& json)
{
    DockingConfig config;

    // Modbus连接配置
    config.serverAddress = json.value("server_address").toString("192.168.1.201");
    config.serverPort = json.value("server_port").toInt(502);
    config.slaveId = json.value("slave_id").toInt(1);

    // 寄存器地址
    config.controlRegister = json.value("control_register").toInt(0x0010);
    config.statusRegister = json.value("status_register").toInt(0x0011);
    config.positionRegister = json.value("position_register").toInt(0x0012);

    // 控制命令值
    config.extendCommand = json.value("extend_command").toInt(1);
    config.retractCommand = json.value("retract_command").toInt(2);
    config.stopCommand = json.value("stop_command").toInt(0);

    // 状态值
    config.extendedStatus = json.value("extended_status").toInt(1);
    config.retractedStatus = json.value("retracted_status").toInt(2);
    config.movingStatus = json.value("moving_status").toInt(3);

    // 超时配置
    config.moveTimeout = json.value("move_timeout").toInt(30000);
    config.statusPollInterval = json.value("status_poll_interval").toInt(100);
    config.connectionTimeout = json.value("connection_timeout").toInt(5000);

    return config;
}

QJsonObject DockingConfig::toJson() const
{
    QJsonObject json;

    json["server_address"] = serverAddress;
    json["server_port"] = serverPort;
    json["slave_id"] = slaveId;

    json["control_register"] = controlRegister;
    json["status_register"] = statusRegister;
    json["position_register"] = positionRegister;

    json["extend_command"] = extendCommand;
    json["retract_command"] = retractCommand;
    json["stop_command"] = stopCommand;

    json["extended_status"] = extendedStatus;
    json["retracted_status"] = retractedStatus;
    json["moving_status"] = movingStatus;

    json["move_timeout"] = moveTimeout;
    json["status_poll_interval"] = statusPollInterval;
    json["connection_timeout"] = connectionTimeout;

    return json;
}

// ============================================================================
// DockingController 实现
// ============================================================================

DockingController::DockingController(const DockingConfig& config,
                                     QObject* parent)
    : BaseMechanismController("Docking", nullptr, parent)  // 不使用ZMotion驱动
    , m_config(config)
    , m_modbusClient(nullptr)
    , m_dockingState(DockingState::Unknown)
    , m_targetState(DockingState::Unknown)
    , m_isConnected(false)
    , m_isMoving(false)
    , m_lastPosition(0.0)
{
    // 状态轮询定时器
    m_statusTimer = new QTimer(this);
    QObject::connect(m_statusTimer, &QTimer::timeout, this, &DockingController::pollStatus);

    // 超时定时器
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    QObject::connect(m_timeoutTimer, &QTimer::timeout, this, &DockingController::onMoveTimeout);

    qDebug() << QString("[%1] DockingController created, server=%2:%3")
                .arg(mechanismCodeString())
                .arg(m_config.serverAddress)
                .arg(m_config.serverPort);
}

DockingController::~DockingController()
{
    stop();
    disconnect();
}

// ============================================================================
// BaseMechanismController接口实现
// ============================================================================

bool DockingController::initialize()
{
    setState(MechanismState::Initializing, "Initializing docking mechanism (Dh)");

    // 连接到Modbus服务器
    if (!connect()) {
        setError("Failed to connect to Modbus server");
        return false;
    }

    reportProgress(50, "Connected to Modbus server");

    // 读取当前状态
    int status = 0;
    if (readStatusRegister(status)) {
        m_dockingState = parseStatus(status);
        emit dockingStateChanged(m_dockingState);
    }

    reportProgress(100, "Initialization complete");

    setState(MechanismState::Ready, "Docking mechanism (Dh) ready");
    emit initialized();
    return true;
}

bool DockingController::stop()
{
    // 停止定时器
    m_statusTimer->stop();
    m_timeoutTimer->stop();

    // 发送停止命令
    if (m_isConnected && m_isMoving) {
        writeControlRegister(m_config.stopCommand);
    }

    m_isMoving = false;
    setState(MechanismState::Holding, "Stopped");

    return true;
}

bool DockingController::reset()
{
    stop();
    m_dockingState = DockingState::Unknown;
    m_targetState = DockingState::Unknown;
    emit dockingStateChanged(m_dockingState);
    setState(MechanismState::Ready, "Reset complete");
    return true;
}

void DockingController::updateStatus()
{
    if (!m_isConnected) {
        return;
    }

    // 读取状态
    int status = 0;
    if (readStatusRegister(status)) {
        DockingState newState = parseStatus(status);
        if (newState != m_dockingState) {
            m_dockingState = newState;
            emit dockingStateChanged(m_dockingState);
        }
    }

    // 读取位置
    double pos = 0.0;
    if (readPositionRegister(pos)) {
        if (qAbs(pos - m_lastPosition) > 0.1) {
            m_lastPosition = pos;
            emit positionChanged(pos);
        }
    }
}

// ============================================================================
// 对接控制
// ============================================================================

bool DockingController::extend()
{
    if (!m_isConnected) {
        setError("Not connected to Modbus server");
        return false;
    }

    if (m_dockingState == DockingState::Extended) {
        qDebug() << QString("[%1] Already extended").arg(mechanismCodeString());
        return true;
    }

    // 发送伸出命令
    if (!writeControlRegister(m_config.extendCommand)) {
        setError("Failed to send extend command");
        return false;
    }

    m_isMoving = true;
    m_targetState = DockingState::Extended;
    m_dockingState = DockingState::Moving;
    setState(MechanismState::Moving, "Extending docking mechanism");
    emit dockingStateChanged(m_dockingState);

    // 启动状态轮询
    m_statusTimer->start(m_config.statusPollInterval);
    m_timeoutTimer->start(m_config.moveTimeout);

    qDebug() << QString("[%1] Extend command sent").arg(mechanismCodeString());

    return true;
}

bool DockingController::retract()
{
    if (!m_isConnected) {
        setError("Not connected to Modbus server");
        return false;
    }

    if (m_dockingState == DockingState::Retracted) {
        qDebug() << QString("[%1] Already retracted").arg(mechanismCodeString());
        return true;
    }

    // 发送收回命令
    if (!writeControlRegister(m_config.retractCommand)) {
        setError("Failed to send retract command");
        return false;
    }

    m_isMoving = true;
    m_targetState = DockingState::Retracted;
    m_dockingState = DockingState::Moving;
    setState(MechanismState::Moving, "Retracting docking mechanism");
    emit dockingStateChanged(m_dockingState);

    // 启动状态轮询
    m_statusTimer->start(m_config.statusPollInterval);
    m_timeoutTimer->start(m_config.moveTimeout);

    qDebug() << QString("[%1] Retract command sent").arg(mechanismCodeString());

    return true;
}

double DockingController::currentPosition() const
{
    return m_lastPosition;
}

// ============================================================================
// 连接管理
// ============================================================================

bool DockingController::connect()
{
    if (m_isConnected) {
        return true;
    }

    qDebug() << QString("[%1] Connecting to %2:%3")
                .arg(mechanismCodeString())
                .arg(m_config.serverAddress)
                .arg(m_config.serverPort);

    // 创建Modbus客户端
    if (!m_modbusClient) {
        m_modbusClient = new QModbusTcpClient(this);
    }

    // 如果已经连接，先断开
    if (m_modbusClient->state() == QModbusDevice::ConnectedState) {
        m_modbusClient->disconnectDevice();
        QThread::msleep(100);
    }

    // 设置连接参数
    m_modbusClient->setConnectionParameter(QModbusDevice::NetworkPortParameter, m_config.serverPort);
    m_modbusClient->setConnectionParameter(QModbusDevice::NetworkAddressParameter, m_config.serverAddress);
    m_modbusClient->setTimeout(m_config.connectionTimeout);
    m_modbusClient->setNumberOfRetries(3);

    // 连接设备
    if (!m_modbusClient->connectDevice()) {
        QString errorMsg = QString("Failed to initiate connection: %1").arg(m_modbusClient->errorString());
        qWarning() << QString("[%1] %2").arg(mechanismCodeString()).arg(errorMsg);
        return false;
    }

    // 等待连接完成
    int waitMs = 0;
    while (m_modbusClient->state() == QModbusDevice::ConnectingState &&
           waitMs < m_config.connectionTimeout) {
        QThread::msleep(100);
        waitMs += 100;
    }

    // 检查连接状态
    if (m_modbusClient->state() != QModbusDevice::ConnectedState) {
        QString errorMsg = QString("Connection failed: %1").arg(m_modbusClient->errorString());
        qWarning() << QString("[%1] %2").arg(mechanismCodeString()).arg(errorMsg);
        return false;
    }

    m_isConnected = true;
    emit connectionStateChanged(true);
    qDebug() << QString("[%1] Connected successfully").arg(mechanismCodeString());

    return true;
}

void DockingController::disconnect()
{
    if (!m_isConnected) {
        return;
    }

    qDebug() << QString("[%1] Disconnecting...").arg(mechanismCodeString());

    if (m_modbusClient && m_modbusClient->state() == QModbusDevice::ConnectedState) {
        m_modbusClient->disconnectDevice();

        // 等待断开完成
        int waitMs = 0;
        while (m_modbusClient->state() != QModbusDevice::UnconnectedState && waitMs < 2000) {
            QThread::msleep(100);
            waitMs += 100;
        }
    }

    m_isConnected = false;
    emit connectionStateChanged(false);
    qDebug() << QString("[%1] Disconnected").arg(mechanismCodeString());
}

bool DockingController::testConnection()
{
    if (!connect()) {
        return false;
    }

    // 尝试读取状态寄存器
    int status = 0;
    if (!readStatusRegister(status)) {
        qWarning() << QString("[%1] Connected but failed to read status")
                      .arg(mechanismCodeString());
        disconnect();
        return false;
    }

    qDebug() << QString("[%1] Connection test successful, status=%2")
                .arg(mechanismCodeString()).arg(status);

    return true;
}

// ============================================================================
// 私有槽函数
// ============================================================================

void DockingController::pollStatus()
{
    if (!m_isConnected || !m_isMoving) {
        m_statusTimer->stop();
        return;
    }

    int status = 0;
    if (!readStatusRegister(status)) {
        return;
    }

    DockingState currentState = parseStatus(status);

    // 检查是否到达目标位置
    if (currentState == m_targetState) {
        m_statusTimer->stop();
        m_timeoutTimer->stop();
        m_isMoving = false;

        m_dockingState = currentState;
        setState(MechanismState::Ready,
                 currentState == DockingState::Extended ? "Extended" : "Retracted");
        emit dockingStateChanged(m_dockingState);
        emit moveCompleted(true);

        qDebug() << QString("[%1] Move completed successfully")
                    .arg(mechanismCodeString());
    }
}

void DockingController::onMoveTimeout()
{
    if (m_isMoving) {
        m_statusTimer->stop();
        m_isMoving = false;

        setError("Move timeout");
        emit moveCompleted(false);

        qWarning() << QString("[%1] Move timeout").arg(mechanismCodeString());
    }
}

// ============================================================================
// Modbus通信
// ============================================================================

bool DockingController::writeControlRegister(int value)
{
    if (!m_isConnected || !m_modbusClient) {
        return false;
    }

    QModbusDataUnit writeUnit(QModbusDataUnit::HoldingRegisters,
                              m_config.controlRegister, 1);
    writeUnit.setValue(0, static_cast<quint16>(value));

    QModbusReply* reply = m_modbusClient->sendWriteRequest(writeUnit, m_config.slaveId);
    if (!reply) {
        return false;
    }

    // 等待响应
    if (!reply->isFinished()) {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);

        QObject::connect(reply, &QModbusReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        timer.start(2000);
        loop.exec();
    }

    bool success = (reply->error() == QModbusDevice::NoError);
    reply->deleteLater();

    return success;
}

bool DockingController::readStatusRegister(int& value)
{
    if (!m_isConnected || !m_modbusClient) {
        return false;
    }

    QModbusDataUnit readUnit(QModbusDataUnit::HoldingRegisters,
                             m_config.statusRegister, 1);

    QModbusReply* reply = m_modbusClient->sendReadRequest(readUnit, m_config.slaveId);
    if (!reply) {
        return false;
    }

    // 等待响应
    if (!reply->isFinished()) {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);

        QObject::connect(reply, &QModbusReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        timer.start(2000);
        loop.exec();
    }

    bool success = false;
    if (reply->error() == QModbusDevice::NoError) {
        QModbusDataUnit result = reply->result();
        if (result.valueCount() >= 1) {
            value = result.value(0);
            success = true;
        }
    }

    reply->deleteLater();
    return success;
}

bool DockingController::readPositionRegister(double& value)
{
    if (!m_isConnected || !m_modbusClient) {
        return false;
    }

    // 读取2个寄存器（32位float）
    QModbusDataUnit readUnit(QModbusDataUnit::HoldingRegisters,
                             m_config.positionRegister, 2);

    QModbusReply* reply = m_modbusClient->sendReadRequest(readUnit, m_config.slaveId);
    if (!reply) {
        return false;
    }

    // 等待响应
    if (!reply->isFinished()) {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);

        QObject::connect(reply, &QModbusReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        timer.start(2000);
        loop.exec();
    }

    bool success = false;
    if (reply->error() == QModbusDevice::NoError) {
        QModbusDataUnit result = reply->result();
        if (result.valueCount() >= 2) {
            // 大端序
            quint16 high = result.value(0);
            quint16 low = result.value(1);
            quint32 rawValue = (static_cast<quint32>(high) << 16) | low;

            float floatValue;
            memcpy(&floatValue, &rawValue, sizeof(float));
            value = static_cast<double>(floatValue);
            success = true;
        }
    }

    reply->deleteLater();
    return success;
}

DockingState DockingController::parseStatus(int statusValue)
{
    if (statusValue == m_config.extendedStatus) {
        return DockingState::Extended;
    } else if (statusValue == m_config.retractedStatus) {
        return DockingState::Retracted;
    } else if (statusValue == m_config.movingStatus) {
        return DockingState::Moving;
    }
    return DockingState::Unknown;
}

// ============================================================================
// 关键位置
// ============================================================================

double DockingController::getKeyPosition(const QString& key) const
{
    if (m_config.keyPositions.contains(key)) {
        return m_config.keyPositions.value(key);
    }
    return -1.0;
}

QStringList DockingController::keyPositionNames() const
{
    return m_config.keyPositions.keys();
}

// ============================================================================
// 配置热更新
// ============================================================================

void DockingController::updateConfig(const DockingConfig& config)
{
    qDebug() << QString("[%1] Updating config").arg(mechanismCodeString());

    // 注意：Modbus连接参数变化时不自动重连，需要手动断开再连接
    bool needReconnect = (m_config.serverAddress != config.serverAddress ||
                          m_config.serverPort != config.serverPort);

    m_config = config;

    if (needReconnect && m_isConnected) {
        qDebug() << QString("[%1] Server address changed, please reconnect").arg(mechanismCodeString());
    }

    qDebug() << QString("[%1] Config updated").arg(mechanismCodeString());
}
