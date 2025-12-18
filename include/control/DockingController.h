#ifndef DOCKINGCONTROLLER_H
#define DOCKINGCONTROLLER_H

#include "control/BaseMechanismController.h"
#include "control/MechanismTypes.h"
#include "control/MechanismDefs.h"
#include <QTimer>
#include <QModbusTcpClient>

/**
 * @brief 对接推杆状态
 */
enum class DockingState {
    Unknown,
    Retracted,      // 收回
    Extended,       // 伸出
    Moving          // 运动中
};

/**
 * @brief 对接推杆配置
 */
struct DockingConfig {
    // Modbus连接配置
    QString serverAddress = "192.168.1.201";
    int serverPort = 502;
    int slaveId = 1;

    // 寄存器地址配置
    int controlRegister = 0x0010;       // 控制寄存器地址
    int statusRegister = 0x0011;        // 状态寄存器地址
    int positionRegister = 0x0012;      // 位置寄存器地址

    // 控制命令值
    int extendCommand = 1;              // 伸出命令
    int retractCommand = 2;             // 收回命令
    int stopCommand = 0;                // 停止命令

    // 状态值
    int extendedStatus = 1;             // 伸出到位状态
    int retractedStatus = 2;            // 收回到位状态
    int movingStatus = 3;               // 运动中状态

    // 超时配置
    int moveTimeout = 30000;            // 运动超时(ms)
    int statusPollInterval = 100;       // 状态查询间隔(ms)
    int connectionTimeout = 5000;       // 连接超时(ms)

    // 关键位置 (A=完全推出, B=完全收回)
    QMap<QString, double> keyPositions;

    static DockingConfig fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
};

/**
 * @brief 对接推杆控制器 (Dh)
 *
 * 功能：
 * 1. Modbus TCP通信
 * 2. 推杆伸出/收回控制
 * 3. 到位状态监控
 *
 * 机构代号: Dh (Docking Head)
 * 通信方式: Modbus TCP
 */
class DockingController : public BaseMechanismController
{
    Q_OBJECT

public:
    static constexpr Mechanism::Code MechanismCode = Mechanism::Dh;

    explicit DockingController(const DockingConfig& config,
                              QObject* parent = nullptr);
    ~DockingController() override;

    // BaseMechanismController接口
    bool initialize() override;
    bool stop() override;
    bool reset() override;
    void updateStatus() override;

    // ========================================================================
    // 对接控制
    // ========================================================================

    /**
     * @brief 伸出推杆
     */
    bool extend();

    /**
     * @brief 收回推杆
     */
    bool retract();

    /**
     * @brief 获取当前状态
     */
    DockingState dockingState() const { return m_dockingState; }

    /**
     * @brief 是否已连接
     */
    bool isConnected() const { return m_isConnected; }

    /**
     * @brief 获取当前位置
     */
    double currentPosition() const;

    // ========================================================================
    // 连接管理
    // ========================================================================

    /**
     * @brief 连接到Modbus服务器
     */
    bool connect();

    /**
     * @brief 断开连接
     */
    void disconnect();

    /**
     * @brief 测试连接
     */
    bool testConnection();

    // ========================================================================
    // 关键位置
    // ========================================================================

    /**
     * @brief 获取关键位置值
     * @param key 位置代号 (A-B)
     * @return 位置值，不存在返回-1
     */
    double getKeyPosition(const QString& key) const;

    /**
     * @brief 获取所有关键位置名称
     */
    QStringList keyPositionNames() const;

    // ========================================================================
    // 机构代号接口
    // ========================================================================

    Mechanism::Code mechanismCode() const { return MechanismCode; }
    QString mechanismCodeString() const { return Mechanism::getCodeString(MechanismCode); }

    /**
     * @brief 更新配置（热更新）
     */
    void updateConfig(const DockingConfig& config);

signals:
    void dockingStateChanged(DockingState state);
    void connectionStateChanged(bool connected);
    void positionChanged(double position);
    void moveCompleted(bool success);

private slots:
    void pollStatus();
    void onMoveTimeout();

private:
    bool writeControlRegister(int value);
    bool readStatusRegister(int& value);
    bool readPositionRegister(double& value);
    DockingState parseStatus(int statusValue);

private:
    DockingConfig m_config;

    QModbusTcpClient* m_modbusClient;
    QTimer* m_statusTimer;
    QTimer* m_timeoutTimer;

    DockingState m_dockingState;
    DockingState m_targetState;
    bool m_isConnected;
    bool m_isMoving;
    double m_lastPosition;
};

#endif // DOCKINGCONTROLLER_H
