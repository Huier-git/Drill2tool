#ifndef IMOTIONDRIVER_H
#define IMOTIONDRIVER_H

#include "control/MechanismTypes.h"
#include <QObject>
#include <QString>

/**
 * @brief 运动控制驱动接口
 * 
 * 定义与运动控制器交互的标准接口，隔离具体的硬件实现细节。
 * 便于：
 * 1. 单元测试时使用Mock对象
 * 2. 支持不同的运动控制器（ZMotion, EtherCAT等）
 * 3. 统一错误处理和日志记录
 */
class IMotionDriver {
public:
    virtual ~IMotionDriver() = default;
    
    // ========================================================================
    // 连接管理
    // ========================================================================
    
    /**
     * @brief 连接到控制器
     * @param connectionString 连接字符串（如IP地址）
     * @return 成功返回true
     */
    virtual bool connect(const QString& connectionString) = 0;
    
    /**
     * @brief 断开连接
     */
    virtual void disconnect() = 0;
    
    /**
     * @brief 检查是否已连接
     */
    virtual bool isConnected() const = 0;
    
    /**
     * @brief 初始化总线
     */
    virtual bool initBus() = 0;
    
    // ========================================================================
    // 轴使能控制
    // ========================================================================
    
    /**
     * @brief 设置轴使能状态
     * @param axis 轴号
     * @param enable true=使能，false=去使能
     * @return 成功返回true
     */
    virtual bool setAxisEnable(int axis, bool enable) = 0;
    
    /**
     * @brief 获取轴使能状态
     */
    virtual bool getAxisEnable(int axis) const = 0;
    
    // ========================================================================
    // 位置控制
    // ========================================================================
    
    /**
     * @brief 设置目标位置（DPOS）
     */
    virtual bool setTargetPosition(int axis, double position) = 0;
    
    /**
     * @brief 获取目标位置（DPOS）
     */
    virtual double getTargetPosition(int axis) const = 0;

    /**
     * @brief 设置实际位置（MPOS）
     */
    virtual bool setActualPosition(int axis, double position) = 0;

    /**
     * @brief 获取实际位置（MPOS）
     */
    virtual double getActualPosition(int axis) const = 0;
    
    // ========================================================================
    // 速度控制
    // ========================================================================
    
    /**
     * @brief 设置速度
     */
    virtual bool setSpeed(int axis, double speed) = 0;
    
    /**
     * @brief 获取目标速度
     */
    virtual double getSpeed(int axis) const = 0;

    /**
     * @brief 获取实际速度
     */
    virtual double getActualVelocity(int axis) const = 0;
    
    // ========================================================================
    // 加减速控制
    // ========================================================================
    
    /**
     * @brief 设置加速度
     */
    virtual bool setAcceleration(int axis, double accel) = 0;
    
    /**
     * @brief 设置减速度
     */
    virtual bool setDeceleration(int axis, double decel) = 0;
    
    /**
     * @brief 获取加速度
     */
    virtual double getAcceleration(int axis) const = 0;

    /**
     * @brief 获取减速度
     */
    virtual double getDeceleration(int axis) const = 0;
    
    // ========================================================================
    // 轴类型和模式
    // ========================================================================
    
    /**
     * @brief 设置轴类型（位置/速度/力矩模式）
     * @param type 65=位置, 66=速度, 67=力矩
     */
    virtual bool setAxisType(int axis, int type) = 0;
    
    /**
     * @brief 获取轴类型
     */
    virtual int getAxisType(int axis) const = 0;

    /**
     * @brief 设置DAC输出（用于力矩模式）
     */
    virtual bool setDAC(int axis, double dac) = 0;

    /**
     * @brief 获取DAC输出
     */
    virtual double getDAC(int axis) const = 0;
    
    // ========================================================================
    // 运动指令
    // ========================================================================
    
    /**
     * @brief 绝对位置运动
     */
    virtual bool moveAbsolute(int axis, double position) = 0;
    
    /**
     * @brief 相对位置运动
     */
    virtual bool moveRelative(int axis, double distance) = 0;
    
    /**
     * @brief 连续运动（点动）
     * @param direction 1=正向, -1=负向
     */
    virtual bool moveContinuous(int axis, int direction) = 0;
    
    /**
     * @brief 停止单轴
     * @param mode 停止模式 (0=减速停, 1=急停, 2=取消缓存和当前运动)
     */
    virtual bool stopAxis(int axis, int mode = 2) = 0;
    
    /**
     * @brief 停止所有轴（紧急停止）
     */
    virtual bool stopAll(int mode = 2) = 0;
    
    // ========================================================================
    // 状态查询
    // ========================================================================
    
    /**
     * @brief 检查轴是否在运动
     */
    virtual bool isAxisMoving(int axis) const = 0;

    /**
     * @brief 获取轴状态
     */
    virtual MotorStatus getAxisStatus(int axis) const = 0;
    
    /**
     * @brief 获取最后的错误信息
     */
    virtual QString getLastError() const = 0;
    
    /**
     * @brief 获取错误码
     */
    virtual int getLastErrorCode() const = 0;
};

#endif // IMOTIONDRIVER_H
