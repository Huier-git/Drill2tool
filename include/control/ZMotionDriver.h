#ifndef ZMOTIONDRIVER_H
#define ZMOTIONDRIVER_H

#include "control/IMotionDriver.h"

/**
 * @brief ZMotion控制器驱动实现 - 使用全局handle
 *
 * 线程安全说明：
 * - 使用全局 g_handle 和 g_mutex（定义在 Global.h）
 * - 所有 ZAux_* API 调用都在持有 g_mutex 锁的情况下进行
 * - 此类只负责底层 API 封装，不负责运动互锁
 * - 运动互锁由 MotionLockManager 在上层管理
 */
class ZMotionDriver : public QObject, public IMotionDriver {
    Q_OBJECT

public:
    explicit ZMotionDriver(QObject* parent = nullptr);
    ~ZMotionDriver() override;

    // 实现IMotionDriver接口
    bool connect(const QString& connectionString) override;
    void disconnect() override;
    bool isConnected() const override;
    bool initBus() override;

    bool setAxisEnable(int axis, bool enable) override;
    bool getAxisEnable(int axis) const override;

    bool setTargetPosition(int axis, double position) override;
    double getTargetPosition(int axis) const override;
    bool setActualPosition(int axis, double position) override;
    double getActualPosition(int axis) const override;

    bool setSpeed(int axis, double speed) override;
    double getSpeed(int axis) const override;
    double getActualVelocity(int axis) const override;

    bool setAcceleration(int axis, double accel) override;
    bool setDeceleration(int axis, double decel) override;
    double getAcceleration(int axis) const override;
    double getDeceleration(int axis) const override;

    bool setAxisType(int axis, int type) override;
    int getAxisType(int axis) const override;
    bool setDAC(int axis, double dac) override;
    double getDAC(int axis) const override;

    bool moveAbsolute(int axis, double position) override;
    bool moveRelative(int axis, double distance) override;
    bool moveContinuous(int axis, int direction) override;
    bool stopAxis(int axis, int mode = 2) override;
    bool stopAll(int mode = 2) override;

    bool isAxisMoving(int axis) const override;
    MotorStatus getAxisStatus(int axis) const override;

    QString getLastError() const override;
    int getLastErrorCode() const override;

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString& error);
    void commandExecuted(const QString& command);

private:
    bool checkConnection() const;
    bool checkError(int errorCode, const QString& operation);
    void setError(int code, const QString& message);

    mutable QString m_lastError;        // 最后错误信息
    mutable int m_lastErrorCode;        // 最后错误码
};

#endif // ZMOTIONDRIVER_H
