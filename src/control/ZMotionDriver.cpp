#include "control/ZMotionDriver.h"
#include "control/zmotion.h"
#include "control/zmcaux.h"
#include "Global.h"
#include <QDebug>
#include <QMutexLocker>

// ZMotion错误码
#define ERR_OK 0

ZMotionDriver::ZMotionDriver(QObject* parent)
    : QObject(parent)
    , m_lastErrorCode(0)
{
}

ZMotionDriver::~ZMotionDriver()
{
    disconnect();
}

// ============================================================================
// 连接管理
// ============================================================================

bool ZMotionDriver::connect(const QString& connectionString)
{
    QMutexLocker locker(&g_mutex);

    if (g_handle != nullptr) {
        qDebug() << "[ZMotionDriver] Already connected";
        return true;
    }

    // 打开ZMotion连接
    QByteArray ipBytes = connectionString.toLocal8Bit();
    int result = ZAux_OpenEth(ipBytes.data(), &g_handle);

    if (result != ERR_OK || !g_handle) {
        setError(result, QString("Failed to connect to %1").arg(connectionString));
        qWarning() << m_lastError;
        return false;
    }

    qDebug() << "[ZMotionDriver] Connected to" << connectionString;
    emit connected();
    emit commandExecuted(QString("Connected to %1").arg(connectionString));

    return true;
}

void ZMotionDriver::disconnect()
{
    QMutexLocker locker(&g_mutex);

    if (g_handle) {
        ZAux_Close(g_handle);
        g_handle = nullptr;
    }

    qDebug() << "[ZMotionDriver] Disconnected";
    emit disconnected();
}

bool ZMotionDriver::isConnected() const
{
    QMutexLocker locker(&g_mutex);
    return g_handle != nullptr;
}

bool ZMotionDriver::initBus()
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return false;

    char response[2048];
    int result = ZAux_Execute(g_handle, "RUNTASK 1,ECAT_Init", response, sizeof(response));
    if (!checkError(result, "RUNTASK 1,ECAT_Init")) {
        return false;
    }

    qDebug() << "[ZMotionDriver] Bus initialized:" << response;
    emit commandExecuted(QString("Bus initialized: %1").arg(response));
    return true;
}

// ============================================================================
// 轴使能控制
// ============================================================================

bool ZMotionDriver::setAxisEnable(int axis, bool enable)
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return false;

    int result = ZAux_Direct_SetAxisEnable(g_handle, axis, enable ? 1 : 0);
    if (!checkError(result, QString("SetAxisEnable(%1, %2)").arg(axis).arg(enable))) {
        return false;
    }

    emit commandExecuted(QString("Axis %1 %2").arg(axis).arg(enable ? "Enabled" : "Disabled"));
    return true;
}

bool ZMotionDriver::getAxisEnable(int axis) const
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return false;

    int value = 0;
    int result = ZAux_Direct_GetAxisEnable(g_handle, axis, &value);
    if (!const_cast<ZMotionDriver*>(this)->checkError(result, QString("GetAxisEnable(%1)").arg(axis))) {
        return false;
    }

    return value > 0;
}

// ============================================================================
// 位置控制
// ============================================================================

bool ZMotionDriver::setTargetPosition(int axis, double position)
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return false;

    int result = ZAux_Direct_SetDpos(g_handle, axis, static_cast<float>(position));
    return checkError(result, QString("SetDpos(%1, %2)").arg(axis).arg(position));
}

double ZMotionDriver::getTargetPosition(int axis) const
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return 0.0;

    float value = 0;
    int result = ZAux_Direct_GetDpos(g_handle, axis, &value);
    if (!const_cast<ZMotionDriver*>(this)->checkError(result, QString("GetDpos(%1)").arg(axis))) {
        return 0.0;
    }

    return static_cast<double>(value);
}

bool ZMotionDriver::setActualPosition(int axis, double position)
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return false;

    int result = ZAux_Direct_SetMpos(g_handle, axis, static_cast<float>(position));
    return checkError(result, QString("SetMpos(%1, %2)").arg(axis).arg(position));
}

double ZMotionDriver::getActualPosition(int axis) const
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return 0.0;

    float value = 0;
    int result = ZAux_Direct_GetMpos(g_handle, axis, &value);
    if (!const_cast<ZMotionDriver*>(this)->checkError(result, QString("GetMpos(%1)").arg(axis))) {
        return 0.0;
    }

    return static_cast<double>(value);
}

// ============================================================================
// 速度控制
// ============================================================================

bool ZMotionDriver::setSpeed(int axis, double speed)
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return false;

    int result = ZAux_Direct_SetSpeed(g_handle, axis, static_cast<float>(speed));
    return checkError(result, QString("SetSpeed(%1, %2)").arg(axis).arg(speed));
}

double ZMotionDriver::getSpeed(int axis) const
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return 0.0;

    float value = 0;
    int result = ZAux_Direct_GetSpeed(g_handle, axis, &value);
    if (!const_cast<ZMotionDriver*>(this)->checkError(result, QString("GetSpeed(%1)").arg(axis))) {
        return 0.0;
    }

    return static_cast<double>(value);
}

double ZMotionDriver::getActualVelocity(int axis) const
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return 0.0;

    float value = 0;
    int result = ZAux_Direct_GetMspeed(g_handle, axis, &value);
    if (!const_cast<ZMotionDriver*>(this)->checkError(result, QString("GetMspeed(%1)").arg(axis))) {
        return 0.0;
    }

    return static_cast<double>(value);
}

// ============================================================================
// 加减速控制
// ============================================================================

bool ZMotionDriver::setAcceleration(int axis, double accel)
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return false;

    int result = ZAux_Direct_SetAccel(g_handle, axis, static_cast<float>(accel));
    return checkError(result, QString("SetAccel(%1, %2)").arg(axis).arg(accel));
}

bool ZMotionDriver::setDeceleration(int axis, double decel)
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return false;

    int result = ZAux_Direct_SetDecel(g_handle, axis, static_cast<float>(decel));
    return checkError(result, QString("SetDecel(%1, %2)").arg(axis).arg(decel));
}

double ZMotionDriver::getAcceleration(int axis) const
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return 0.0;

    float value = 0;
    int result = ZAux_Direct_GetAccel(g_handle, axis, &value);
    if (!const_cast<ZMotionDriver*>(this)->checkError(result, QString("GetAccel(%1)").arg(axis))) {
        return 0.0;
    }

    return static_cast<double>(value);
}

double ZMotionDriver::getDeceleration(int axis) const
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return 0.0;

    float value = 0;
    int result = ZAux_Direct_GetDecel(g_handle, axis, &value);
    if (!const_cast<ZMotionDriver*>(this)->checkError(result, QString("GetDecel(%1)").arg(axis))) {
        return 0.0;
    }

    return static_cast<double>(value);
}

// ============================================================================
// 轴类型和模式
// ============================================================================

bool ZMotionDriver::setAxisType(int axis, int type)
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return false;

    int result = ZAux_Direct_SetAtype(g_handle, axis, type);
    if (!checkError(result, QString("SetAtype(%1, %2)").arg(axis).arg(type))) {
        return false;
    }

    QString modeStr;
    switch (type) {
        case 65: modeStr = "Position"; break;
        case 66: modeStr = "Velocity"; break;
        case 67: modeStr = "Torque"; break;
        default: modeStr = QString::number(type); break;
    }

    emit commandExecuted(QString("Axis %1 set to %2 mode").arg(axis).arg(modeStr));
    return true;
}

int ZMotionDriver::getAxisType(int axis) const
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return 0;

    int value = 0;
    int result = ZAux_Direct_GetAtype(g_handle, axis, &value);
    if (!const_cast<ZMotionDriver*>(this)->checkError(result, QString("GetAtype(%1)").arg(axis))) {
        return 0;
    }

    return value;
}

bool ZMotionDriver::setDAC(int axis, double dac)
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return false;

    int result = ZAux_Direct_SetDAC(g_handle, axis, static_cast<float>(dac));
    return checkError(result, QString("SetDAC(%1, %2)").arg(axis).arg(dac));
}

double ZMotionDriver::getDAC(int axis) const
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return 0.0;

    float value = 0;
    int result = ZAux_Direct_GetDAC(g_handle, axis, &value);
    if (!const_cast<ZMotionDriver*>(this)->checkError(result, QString("GetDAC(%1)").arg(axis))) {
        return 0.0;
    }

    return static_cast<double>(value);
}

// ============================================================================
// 运动指令
// ============================================================================

bool ZMotionDriver::moveAbsolute(int axis, double position)
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return false;

    int result = ZAux_Direct_Single_MoveAbs(g_handle, axis, static_cast<float>(position));
    if (!checkError(result, QString("MoveAbs(%1, %2)").arg(axis).arg(position))) {
        return false;
    }

    emit commandExecuted(QString("Axis %1 MoveAbs to %2").arg(axis).arg(position));
    return true;
}

bool ZMotionDriver::moveRelative(int axis, double distance)
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return false;

    int result = ZAux_Direct_Single_Move(g_handle, axis, static_cast<float>(distance));
    if (!checkError(result, QString("Move(%1, %2)").arg(axis).arg(distance))) {
        return false;
    }

    emit commandExecuted(QString("Axis %1 Move %2").arg(axis).arg(distance));
    return true;
}

bool ZMotionDriver::moveContinuous(int axis, int direction)
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return false;

    if (direction == 0) {
        // 不持锁调用 stopAxis，需要先解锁
        locker.unlock();
        return stopAxis(axis, 2);
    }

    int result = ZAux_Direct_Single_Vmove(g_handle, axis, direction > 0 ? 1 : -1);
    if (!checkError(result, QString("Vmove(%1, %2)").arg(axis).arg(direction))) {
        return false;
    }

    emit commandExecuted(QString("Axis %1 Jog %2").arg(axis).arg(direction > 0 ? "+" : "-"));
    return true;
}

bool ZMotionDriver::stopAxis(int axis, int mode)
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return false;

    int result = ZAux_Direct_Single_Cancel(g_handle, axis, mode);
    if (!checkError(result, QString("StopAxis(%1, mode=%2)").arg(axis).arg(mode))) {
        return false;
    }

    emit commandExecuted(QString("Axis %1 Stopped").arg(axis));
    return true;
}

bool ZMotionDriver::stopAll(int mode)
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return false;

    int result = ZAux_Direct_Rapidstop(g_handle, mode);
    if (!checkError(result, QString("StopAll(mode=%1)").arg(mode))) {
        return false;
    }

    qWarning() << "[ZMotionDriver] EMERGENCY STOP EXECUTED";
    emit commandExecuted("EMERGENCY STOP - ALL MOTORS STOPPED");
    return true;
}

// ============================================================================
// 状态查询
// ============================================================================

bool ZMotionDriver::isAxisMoving(int axis) const
{
    QMutexLocker locker(&g_mutex);
    if (!checkConnection()) return false;

    int status = 0;
    int result = ZAux_Direct_GetIfIdle(g_handle, axis, &status);
    if (!const_cast<ZMotionDriver*>(this)->checkError(result, QString("GetIfIdle(%1)").arg(axis))) {
        return false;
    }

    // status == 0 表示在运动，非0表示空闲
    return (status == 0);
}

MotorStatus ZMotionDriver::getAxisStatus(int axis) const
{
    QMutexLocker locker(&g_mutex);

    MotorStatus status;
    status.motorId = axis;

    if (!checkConnection()) {
        return status;
    }

    // 获取使能状态
    int enableValue = 0;
    if (ZAux_Direct_GetAxisEnable(g_handle, axis, &enableValue) == ERR_OK) {
        status.enabled = (enableValue > 0);
    }

    // 获取实际位置
    float mpos = 0;
    if (ZAux_Direct_GetMpos(g_handle, axis, &mpos) == ERR_OK) {
        status.actualPosition = static_cast<double>(mpos);
    }

    // 获取目标位置
    float dpos = 0;
    if (ZAux_Direct_GetDpos(g_handle, axis, &dpos) == ERR_OK) {
        status.targetPosition = static_cast<double>(dpos);
    }

    // 获取实际速度
    float curSpeed = 0;
    if (ZAux_Direct_GetMspeed(g_handle, axis, &curSpeed) == ERR_OK) {
        status.actualVelocity = static_cast<double>(curSpeed);
    }

    // 获取目标速度
    float speed = 0;
    if (ZAux_Direct_GetSpeed(g_handle, axis, &speed) == ERR_OK) {
        status.targetVelocity = static_cast<double>(speed);
    }

    // 获取DAC输出
    float dac = 0;
    if (ZAux_Direct_GetDAC(g_handle, axis, &dac) == ERR_OK) {
        status.dacOutput = static_cast<double>(dac);
    }

    // 获取轴类型
    int atype = 0;
    if (ZAux_Direct_GetAtype(g_handle, axis, &atype) == ERR_OK) {
        status.mode = static_cast<MotorMode>(atype);
    }

    return status;
}

QString ZMotionDriver::getLastError() const
{
    return m_lastError;
}

int ZMotionDriver::getLastErrorCode() const
{
    return m_lastErrorCode;
}

// ============================================================================
// 私有辅助函数
// ============================================================================

bool ZMotionDriver::checkConnection() const
{
    if (!g_handle) {
        const_cast<ZMotionDriver*>(this)->setError(-1, "Not connected to controller");
        return false;
    }
    return true;
}

bool ZMotionDriver::checkError(int errorCode, const QString& operation)
{
    if (errorCode == ERR_OK) {
        m_lastErrorCode = 0;
        m_lastError.clear();
        return true;
    }

    setError(errorCode, QString("Operation '%1' failed with error code %2")
                        .arg(operation)
                        .arg(errorCode));

    qWarning() << "[ZMotionDriver]" << m_lastError;
    emit errorOccurred(m_lastError);

    return false;
}

void ZMotionDriver::setError(int code, const QString& message)
{
    m_lastErrorCode = code;
    m_lastError = message;
}
