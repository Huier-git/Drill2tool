#include "Global.h"

// 全局ZMotion控制器句柄
ZMC_HANDLE g_handle = nullptr;

// 全局互斥锁 - 保护 g_handle 的所有访问
QMutex g_mutex;

// 电机映射表，EtherCAT的映射关系
// 参考原项目zmotionpage.cpp:9-20
int MotorMap[10] = {
    0,  // MOTOR_IDX_ROTATION (旋转切割电机)
    1,  // MOTOR_IDX_PERCUSSION (冲击电机)
    2,  // MOTOR_IDX_PENETRATION (进给电机)
    3,  // MOTOR_IDX_DOWNCLAMP (下夹紧电机)
    4,  // MOTOR_IDX_ROBOTCLAMP (机械手夹紧电机)
    5,  // MOTOR_IDX_ROBOTROTATION (机械手旋转电机)
    6,  // MOTOR_IDX_ROBOTEXTENSION (机械手移动电机)
    7,  // MOTOR_IDX_STORAGE (存储电机)
    8,  // M8
    9   // M9
};
