#ifndef MOTIONCONFIGMANAGER_H
#define MOTIONCONFIGMANAGER_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMap>
#include <QFileSystemWatcher>
#include "control/MechanismDefs.h"
#include "control/MechanismTypes.h"

// 前向声明新控制器的配置结构
struct RotationConfig;
struct PercussionConfig;
struct ArmExtensionConfig;
struct ArmGripConfig;
struct ArmRotationConfig;
struct DockingConfig;

/**
 * @brief 机构配置参数结构（通用）
 */
struct MechanismParams {
    QString name;               // 机构名称
    QString code;               // 机构代号 (Fz, Sr, Me, etc.)
    int motorId = -1;           // 电机ID (-1表示Modbus)
    QString controlMode;        // 控制模式 (position/velocity/torque)
    QString connectionType;     // 连接类型 (ethercat/modbus)

    // 通用运动参数
    double speed = 100.0;
    double acceleration = 100.0;
    double deceleration = 100.0;
    double maxPosition = 1e6;
    double minPosition = -1e6;

    // 力矩控制参数
    double openDac = -100.0;
    double closeDac = 100.0;
    double initDac = 0.0;
    double dacIncrement = 10.0;

    // 位置控制参数
    double pulsesPerMm = 1.0;
    double pulsesPerDegree = 1.0;
    bool hasPulsesPerMm = false;
    bool hasPulsesPerDegree = false;
    int positions = 1;          // 离散位置数（存储机构用）
    double anglePerPosition = 0.0;

    // 限位参数（旧版兼容）
    double safePosition = 0.0;
    double workPosition = 0.0;

    // 关键位置参数 (A-J)
    // Fz: A=最底端, B=钻管底端对接结束, C=钻管底端对接开始, D=钻管顶端对接结束,
    //     E=钻具顶端对接结束, F=钻管顶端对接开始, G=钻具顶端对接开始, H=最顶端,
    //     I=搭载钻管后底部对接结束, J=搭载钻管后顶部对接开始
    // Sr: A=位置0, B-G=位置1-6
    // Me: A=完全收回, B=面对存储机构, C=面对对接头
    // Mg: A=完全张开, B=完全夹紧
    // Mr: A=对准存储机构, B=对准对接头
    // Dh: A=完全推出, B=完全收回
    // Pr: A=不旋转, B=正向对接速度, C=逆向对接速度, D=程序调控速度
    // Pi: A=不冲击, B=程序调控冲击
    // Cb: A=完全张开, B=完全夹紧
    QMap<QString, double> keyPositions;

    // 初始化参数
    double stableThreshold = 1.0;
    int stableCount = 5;
    int monitorInterval = 500;
    double positionTolerance = 100.0;

    // Modbus特有参数（对接头）
    int modbusDevice = -1;
    int slaveId = 1;
    int extendPosition = 0;
    int retractPosition = 0;

    // JSON序列化
    static MechanismParams fromJson(const QJsonObject& json);
    QJsonObject toJson() const;

    // 关键位置辅助方法
    double getKeyPosition(const QString& key, double defaultValue = 0.0) const;
    void setKeyPosition(const QString& key, double value);
    QStringList getKeyPositionNames() const;
};

/**
 * @brief 运动配置管理器
 *
 * 功能：
 * 1. 从JSON文件加载机构配置
 * 2. 提供配置查询接口
 * 3. 支持配置热更新（文件监控）
 * 4. 通知配置变化
 */
class MotionConfigManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 获取单例实例
     */
    static MotionConfigManager* instance();

    /**
     * @brief 加载配置文件
     * @param filePath 配置文件路径
     * @return 成功返回true
     */
    bool loadConfig(const QString& filePath);

    /**
     * @brief 重新加载配置
     */
    bool reloadConfig();

    /**
     * @brief 保存配置到文件
     * @param filePath 文件路径（空则保存到当前加载的文件）
     */
    bool saveConfig(const QString& filePath = QString());

    /**
     * @brief 启用/禁用文件监控（热更新）
     */
    void setFileWatchEnabled(bool enabled);

    // ========================================================================
    // 配置查询接口
    // ========================================================================

    /**
     * @brief 获取机构配置
     * @param code 机构代号
     */
    MechanismParams getMechanismConfig(Mechanism::Code code) const;

    /**
     * @brief 获取机构配置（按代号字符串）
     * @param codeStr 代号字符串 (Fz, Sr, etc.)
     */
    MechanismParams getMechanismConfig(const QString& codeStr) const;

    /**
     * @brief 检查机构配置是否存在
     */
    bool hasMechanismConfig(Mechanism::Code code) const;

    /**
     * @brief 更新机构配置
     * @param code 机构代号
     * @param params 新的参数
     */
    void updateMechanismConfig(Mechanism::Code code, const MechanismParams& params);

    /**
     * @brief 获取所有机构配置
     */
    QMap<Mechanism::Code, MechanismParams> getAllConfigs() const;

    /**
     * @brief 获取配置文件路径
     */
    QString configFilePath() const { return m_configFilePath; }

    /**
     * @brief 获取配置版本
     */
    QString configVersion() const { return m_configVersion; }

    // ========================================================================
    // 转换为专用配置结构
    // ========================================================================

    /**
     * @brief 获取进给机构配置
     */
    PenetrationConfig getPenetrationConfig() const;

    /**
     * @brief 获取钻进配置（回转+冲击）
     */
    DrillConfig getDrillConfig() const;

    /**
     * @brief 获取机械手配置
     */
    RoboticArmConfig getRoboticArmConfig() const;

    /**
     * @brief 获取存储机构配置
     */
    StorageConfig getStorageConfig() const;

    /**
     * @brief 获取夹紧机构配置
     */
    ClampConfig getClampConfig() const;

    // ========================================================================
    // 新控制器配置接口
    // ========================================================================

    // FeedController (Fz) 使用 getPenetrationConfig()

    /**
     * @brief 获取回转控制器配置 (Pr)
     */
    RotationConfig getRotationConfig() const;

    /**
     * @brief 获取冲击控制器配置 (Pi)
     */
    PercussionConfig getPercussionConfig() const;

    /**
     * @brief 获取机械手伸缩配置 (Me)
     */
    ArmExtensionConfig getArmExtensionConfig() const;

    /**
     * @brief 获取机械手夹紧配置 (Mg)
     */
    ArmGripConfig getArmGripConfig() const;

    /**
     * @brief 获取机械手回转配置 (Mr)
     */
    ArmRotationConfig getArmRotationConfig() const;

    /**
     * @brief 获取对接推杆配置 (Dh)
     */
    DockingConfig getDockingConfig() const;

signals:
    /**
     * @brief 配置加载完成信号
     */
    void configLoaded(bool success);

    /**
     * @brief 配置变化信号
     */
    void configChanged();

    /**
     * @brief 特定机构配置变化信号
     */
    void mechanismConfigChanged(Mechanism::Code code);

    /**
     * @brief 错误信号
     */
    void errorOccurred(const QString& error);

private slots:
    void onFileChanged(const QString& path);

private:
    // 私有构造（单例）
    explicit MotionConfigManager(QObject* parent = nullptr);
    ~MotionConfigManager();

    // 禁止拷贝
    MotionConfigManager(const MotionConfigManager&) = delete;
    MotionConfigManager& operator=(const MotionConfigManager&) = delete;

    // 解析配置
    bool parseConfig(const QJsonObject& root);
    MechanismParams parseMechanismConfig(const QString& code, const QJsonObject& json);

    // 成员变量
    QString m_configFilePath;
    QString m_configVersion;
    QMap<Mechanism::Code, MechanismParams> m_configs;
    QFileSystemWatcher* m_fileWatcher;
    bool m_fileWatchEnabled;

    static MotionConfigManager* s_instance;
};

#endif // MOTIONCONFIGMANAGER_H
