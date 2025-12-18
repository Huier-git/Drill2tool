#ifndef MOTIONCONTROLLER_H
#define MOTIONCONTROLLER_H

#include <QObject>
#include <QVector>

/**
 * @brief 运动控制器 Worker
 * 
 * 职责：
 * 1. 接收 ZMotion 连接句柄
 * 2. 执行点动、绝对运动、回零等控制指令
 * 3. 执行急停操作
 * 
 * 注意：
 * 此类不负责连接，只负责发送控制指令。
 * 必须在 setHandle 被调用且 handle 有效后才能工作。
 */
class MotionController : public QObject
{
    Q_OBJECT
    
public:
    explicit MotionController(QObject *parent = nullptr);
    ~MotionController();
    
    // 设置控制句柄（通常由 MotorWorker 连接成功后提供）
    void setHandle(void* handle);
    bool isReady() const { return m_handle != nullptr; }

public slots:
    // --- 轴控制 ---
    void setAxisEnable(int axis, bool enable);          // 使能/去使能
    void zeroAxis(int axis);                            // 归零/设置当前为零点
    void stopAxis(int axis);                            // 停止单轴
    
    // --- 运动指令 ---
    /**
     * @brief 点动控制
     * @param axis 轴号
     * @param direction 1:正向, -1:负向, 0:停止
     * @param speed 速度 (0使用默认)
     */
    void jogMove(int axis, int direction, double speed = 0);
    
    /**
     * @brief 绝对运动
     * @param axis 轴号
     * @param position 目标位置
     * @param speed 速度
     */
    void absMove(int axis, double position, double speed);
    
    // --- 全局控制 ---
    void stopAllMotors();                               // 停止所有轴 (急停)

signals:
    void errorOccurred(const QString &msg);
    void commandExecuted(const QString &cmd);

private:
    void* m_handle;
    
    // 默认运动参数
    const float DEFAULT_ACCEL = 200.0f;
    const float DEFAULT_DECEL = 200.0f;
    const float DEFAULT_SPEED = 100.0f;
};

#endif // MOTIONCONTROLLER_H
