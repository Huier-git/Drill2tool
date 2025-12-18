#include "control/MotionController.h"
#include "zmotion.h"
#include "zmcaux.h"
#include <QDebug>

MotionController::MotionController(QObject *parent)
    : QObject(parent)
    , m_handle(nullptr)
{
}

MotionController::~MotionController()
{
    m_handle = nullptr;
}

void MotionController::setHandle(void* handle)
{
    m_handle = handle;
    if (m_handle) {
        qDebug() << "[MotionController] Handle set, controller ready.";
    } else {
        qDebug() << "[MotionController] Handle cleared, controller disabled.";
    }
}

void MotionController::setAxisEnable(int axis, bool enable)
{
    if (!m_handle) return;
    
    int result = ZAux_Direct_SetAxisEnable(m_handle, axis, enable ? 1 : 0);
    if (result != ERR_OK) {
        emit errorOccurred(QString("Set Axis %1 Enable Failed: %2").arg(axis).arg(result));
    } else {
        emit commandExecuted(QString("Axis %1 %2").arg(axis).arg(enable ? "Enabled" : "Disabled"));
    }
}

void MotionController::zeroAxis(int axis)
{
    if (!m_handle) return;
    
    // 设置当前位置为0 (同时设置 DPOS 和 MPOS)
    ZAux_Direct_SetDpos(m_handle, axis, 0.0f);
    ZAux_Direct_SetMpos(m_handle, axis, 0.0f);
    emit commandExecuted(QString("Axis %1 Zeroed").arg(axis));
}

void MotionController::stopAxis(int axis)
{
    if (!m_handle) return;
    
    // 停止单轴 (mode 2: Cancel buffered and current motion)
    ZAux_Direct_Single_Cancel(m_handle, axis, 2);
    emit commandExecuted(QString("Axis %1 Stopped").arg(axis));
}

void MotionController::stopAllMotors()
{
    if (!m_handle) return;
    
    // 停止所有轴 (mode 2)
    ZAux_Direct_Rapidstop(m_handle, 2);
    emit commandExecuted("ALL MOTORS STOPPED");
}

void MotionController::jogMove(int axis, int direction, double speed)
{
    if (!m_handle) return;
    
    if (direction == 0) {
        stopAxis(axis);
        return;
    }
    
    // 设置参数
    float moveSpeed = (speed > 0) ? static_cast<float>(speed) : DEFAULT_SPEED;
    ZAux_Direct_SetSpeed(m_handle, axis, moveSpeed);
    ZAux_Direct_SetAccel(m_handle, axis, DEFAULT_ACCEL);
    ZAux_Direct_SetDecel(m_handle, axis, DEFAULT_DECEL);
    
    // VMOVE: 连续运动
    int dir = (direction > 0) ? 1 : -1;
    ZAux_Direct_Single_Vmove(m_handle, axis, dir);
    
    emit commandExecuted(QString("Axis %1 Jog %2, Speed %3").arg(axis).arg(dir > 0 ? "+" : "-").arg(moveSpeed));
}

void MotionController::absMove(int axis, double position, double speed)
{
    if (!m_handle) return;
    
    float moveSpeed = (speed > 0) ? static_cast<float>(speed) : DEFAULT_SPEED;
    ZAux_Direct_SetSpeed(m_handle, axis, moveSpeed);
    ZAux_Direct_SetAccel(m_handle, axis, DEFAULT_ACCEL);
    ZAux_Direct_SetDecel(m_handle, axis, DEFAULT_DECEL);
    
    // 绝对运动
    ZAux_Direct_Single_MoveAbs(m_handle, axis, static_cast<float>(position));
    
    emit commandExecuted(QString("Axis %1 MoveAbs to %2").arg(axis).arg(position));
}
