#ifndef GLOBAL_H
#define GLOBAL_H

#include "control/zmotion.h"
#include <QMutex>

// 全局ZMotion控制器句柄
// 参考原项目：C:\Users\YMH\Desktop\drillControl\src\zmotionpage.cpp:5
extern ZMC_HANDLE g_handle;

// 全局互斥锁 - 保护 g_handle 的所有访问
// 所有 ZAux_* API 调用都必须在持有此锁的情况下进行
extern QMutex g_mutex;

// 电机映射表 - EtherCAT映射关系
// 参考原项目的MotorMap定义
extern int MotorMap[10];

#endif // GLOBAL_H
