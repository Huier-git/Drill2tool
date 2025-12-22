#include "control/MotionConfigManager.h"
#include "control/FeedController.h"
#include "control/RotationController.h"
#include "control/PercussionController.h"
#include "control/ArmExtensionController.h"
#include "control/ArmGripController.h"
#include "control/ArmRotationController.h"
#include "control/DockingController.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QJsonArray>
#include <QDebug>
#include <QTimer>

// 静态实例
MotionConfigManager* MotionConfigManager::s_instance = nullptr;

// ============================================================================
// MechanismParams 实现
// ============================================================================

MechanismParams MechanismParams::fromJson(const QJsonObject& json)
{
    MechanismParams params;

    params.name = json.value("name").toString();
    params.code = json.value("code").toString();
    params.motorId = json.value("motor_id").toInt(-1);
    params.controlMode = json.value("control_mode").toString("position");
    params.connectionType = json.value("connection_type").toString("ethercat");

    // 运动参数
    params.speed = json.value("speed").toDouble(100.0);
    params.acceleration = json.value("acceleration").toDouble(100.0);
    params.deceleration = json.value("deceleration").toDouble(100.0);
    params.maxPosition = json.value("max_position").toDouble(1e6);
    params.minPosition = json.value("min_position").toDouble(-1e6);

    // 力矩参数
    params.openDac = json.value("open_dac").toDouble(-100.0);
    params.closeDac = json.value("close_dac").toDouble(100.0);
    params.initDac = json.value("init_dac").toDouble(0.0);
    params.dacIncrement = json.value("dac_increment").toDouble(10.0);

    // 位置参数
    params.hasPulsesPerMm = json.contains("pulses_per_mm");
    params.hasPulsesPerDegree = json.contains("pulses_per_degree");
    params.pulsesPerMm = json.value("pulses_per_mm").toDouble(1.0);
    params.pulsesPerDegree = json.value("pulses_per_degree").toDouble(1.0);
    params.positions = json.value("positions").toInt(1);
    params.anglePerPosition = json.value("angle_per_position").toDouble(0.0);

    // 限位参数
    params.safePosition = json.value("safe_position").toDouble(0.0);
    params.workPosition = json.value("work_position").toDouble(0.0);

    // 关键位置参数
    if (json.contains("key_positions")) {
        QJsonObject keyPosObj = json.value("key_positions").toObject();
        for (auto it = keyPosObj.constBegin(); it != keyPosObj.constEnd(); ++it) {
            params.keyPositions[it.key()] = it.value().toDouble(0.0);
        }
    }

    // 初始化参数
    params.stableThreshold = json.value("stable_threshold").toDouble(1.0);
    params.stableCount = json.value("stable_count").toInt(5);
    params.monitorInterval = json.value("monitor_interval").toInt(500);
    params.positionTolerance = json.value("position_tolerance").toDouble(100.0);

    // Modbus参数
    params.modbusDevice = json.value("modbus_device").toInt(-1);
    params.slaveId = json.value("slave_id").toInt(1);
    params.extendPosition = json.value("extend_position").toInt(0);
    params.retractPosition = json.value("retract_position").toInt(0);

    return params;
}

QJsonObject MechanismParams::toJson() const
{
    QJsonObject json;

    json["name"] = name;
    json["code"] = code;
    json["motor_id"] = motorId;
    json["control_mode"] = controlMode;
    json["connection_type"] = connectionType;

    json["speed"] = speed;
    json["acceleration"] = acceleration;
    json["deceleration"] = deceleration;
    json["max_position"] = maxPosition;
    json["min_position"] = minPosition;

    json["open_dac"] = openDac;
    json["close_dac"] = closeDac;
    json["init_dac"] = initDac;
    json["dac_increment"] = dacIncrement;

    json["pulses_per_mm"] = pulsesPerMm;
    json["pulses_per_degree"] = pulsesPerDegree;
    json["positions"] = positions;
    json["angle_per_position"] = anglePerPosition;

    json["safe_position"] = safePosition;
    json["work_position"] = workPosition;

    // 关键位置参数
    if (!keyPositions.isEmpty()) {
        QJsonObject keyPosObj;
        for (auto it = keyPositions.constBegin(); it != keyPositions.constEnd(); ++it) {
            keyPosObj[it.key()] = it.value();
        }
        json["key_positions"] = keyPosObj;
    }

    json["stable_threshold"] = stableThreshold;
    json["stable_count"] = stableCount;
    json["monitor_interval"] = monitorInterval;
    json["position_tolerance"] = positionTolerance;

    if (connectionType == "modbus") {
        json["modbus_device"] = modbusDevice;
        json["slave_id"] = slaveId;
        json["extend_position"] = extendPosition;
        json["retract_position"] = retractPosition;
    }

    return json;
}

// 关键位置辅助方法
double MechanismParams::getKeyPosition(const QString& key, double defaultValue) const
{
    return keyPositions.value(key, defaultValue);
}

void MechanismParams::setKeyPosition(const QString& key, double value)
{
    keyPositions[key] = value;
}

QStringList MechanismParams::getKeyPositionNames() const
{
    return keyPositions.keys();
}

// ============================================================================
// MotionConfigManager 实现
// ============================================================================

MotionConfigManager* MotionConfigManager::instance()
{
    if (!s_instance) {
        s_instance = new MotionConfigManager();
    }
    return s_instance;
}

MotionConfigManager::MotionConfigManager(QObject* parent)
    : QObject(parent)
    , m_fileWatcher(new QFileSystemWatcher(this))
    , m_fileWatchEnabled(false)
{
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged,
            this, &MotionConfigManager::onFileChanged);
}

MotionConfigManager::~MotionConfigManager()
{
}

bool MotionConfigManager::loadConfig(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(tr("无法打开配置文件: %1").arg(filePath));
        emit configLoaded(false);
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        emit errorOccurred(tr("JSON解析错误: %1").arg(parseError.errorString()));
        emit configLoaded(false);
        return false;
    }

    if (!doc.isObject()) {
        emit errorOccurred(tr("配置文件格式错误：根节点必须是对象"));
        emit configLoaded(false);
        return false;
    }

    m_configFilePath = filePath;

    if (!parseConfig(doc.object())) {
        emit configLoaded(false);
        return false;
    }

    // 更新文件监控
    if (m_fileWatchEnabled) {
        if (!m_fileWatcher->files().isEmpty()) {
            m_fileWatcher->removePaths(m_fileWatcher->files());
        }
        m_fileWatcher->addPath(filePath);
    }

    qDebug() << "[MotionConfigManager] Loaded config:" << filePath
             << "version:" << m_configVersion;

    emit configLoaded(true);
    return true;
}

bool MotionConfigManager::reloadConfig()
{
    if (m_configFilePath.isEmpty()) {
        emit errorOccurred(tr("没有已加载的配置文件"));
        return false;
    }
    return loadConfig(m_configFilePath);
}

bool MotionConfigManager::saveConfig(const QString& filePath)
{
    QString savePath = filePath.isEmpty() ? m_configFilePath : filePath;

    if (savePath.isEmpty()) {
        emit errorOccurred(tr("未指定保存路径"));
        return false;
    }

    QFileInfo info(savePath);
    QDir dir(info.absolutePath());
    if (!dir.exists() && !dir.mkpath(".")) {
        emit errorOccurred(tr("无法创建配置目录: %1").arg(info.absolutePath()));
        return false;
    }

    QJsonObject root;
    root["_version"] = m_configVersion;
    root["_comment"] = "机构运动参数配置文件";

    QJsonObject mechanisms;
    for (auto it = m_configs.constBegin(); it != m_configs.constEnd(); ++it) {
        mechanisms[it.value().code] = it.value().toJson();
    }
    root["mechanisms"] = mechanisms;

    QJsonDocument doc(root);

    QSignalBlocker blocker(m_fileWatcher);

    QSaveFile file(savePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit errorOccurred(tr("无法写入配置文件: %1").arg(savePath));
        return false;
    }

    if (file.write(doc.toJson(QJsonDocument::Indented)) == -1) {
        emit errorOccurred(tr("写入配置文件失败: %1").arg(savePath));
        return false;
    }

    if (!file.commit()) {
        emit errorOccurred(tr("保存配置文件失败: %1").arg(savePath));
        return false;
    }

    m_configFilePath = savePath;
    if (m_fileWatchEnabled && !m_fileWatcher->files().contains(savePath)) {
        m_fileWatcher->addPath(savePath);
    }

    qDebug() << "[MotionConfigManager] Saved config to:" << savePath;
    return true;
}

void MotionConfigManager::setFileWatchEnabled(bool enabled)
{
    m_fileWatchEnabled = enabled;

    if (enabled && !m_configFilePath.isEmpty()) {
        m_fileWatcher->addPath(m_configFilePath);
    } else if (!enabled) {
        if (!m_fileWatcher->files().isEmpty()) {
            m_fileWatcher->removePaths(m_fileWatcher->files());
        }
    }
}

bool MotionConfigManager::parseConfig(const QJsonObject& root)
{
    m_configs.clear();

    m_configVersion = root.value("_version").toString("1.0");

    QJsonObject mechanisms = root.value("mechanisms").toObject();

    for (auto it = mechanisms.constBegin(); it != mechanisms.constEnd(); ++it) {
        QString codeStr = it.key();
        QJsonObject mechJson = it.value().toObject();

        Mechanism::Code code = Mechanism::fromCodeString(codeStr);
        if (static_cast<int>(code) < 0) {
            qWarning() << "[MotionConfigManager] Unknown mechanism code:" << codeStr;
            continue;
        }

        MechanismParams params = parseMechanismConfig(codeStr, mechJson);
        m_configs[code] = params;

        qDebug() << "[MotionConfigManager] Loaded mechanism:" << codeStr
                 << "motorId:" << params.motorId;
    }

    return true;
}

MechanismParams MotionConfigManager::parseMechanismConfig(const QString& code, const QJsonObject& json)
{
    MechanismParams params = MechanismParams::fromJson(json);
    params.code = code;

    // 如果没有设置motorId，使用默认映射
    if (params.motorId < 0 && params.connectionType != "modbus") {
        Mechanism::Code mechCode = Mechanism::fromCodeString(code);
        if (static_cast<int>(mechCode) >= 0) {
            params.motorId = Mechanism::getMotorIndex(mechCode);
        }
    }

    return params;
}

MechanismParams MotionConfigManager::getMechanismConfig(Mechanism::Code code) const
{
    return m_configs.value(code, MechanismParams());
}

MechanismParams MotionConfigManager::getMechanismConfig(const QString& codeStr) const
{
    Mechanism::Code code = Mechanism::fromCodeString(codeStr);
    return getMechanismConfig(code);
}

bool MotionConfigManager::hasMechanismConfig(Mechanism::Code code) const
{
    return m_configs.contains(code);
}

void MotionConfigManager::updateMechanismConfig(Mechanism::Code code, const MechanismParams& params)
{
    m_configs[code] = params;
    qDebug() << "[MotionConfigManager] Updated config for:" << Mechanism::getCodeString(code);
    emit mechanismConfigChanged(code);
}

QMap<Mechanism::Code, MechanismParams> MotionConfigManager::getAllConfigs() const
{
    return m_configs;
}

void MotionConfigManager::onFileChanged(const QString& path)
{
    Q_UNUSED(path)

    qDebug() << "[MotionConfigManager] Config file changed, reloading...";

    // 延迟重新加载，避免文件还在写入
    QTimer::singleShot(500, this, [this]() {
        if (reloadConfig()) {
            emit configChanged();
        }
    });
}

// ============================================================================
// 转换为专用配置结构
// ============================================================================

PenetrationConfig MotionConfigManager::getPenetrationConfig() const
{
    PenetrationConfig config;
    MechanismParams params = getMechanismConfig(Mechanism::Fz);

    config.motor.motorId = params.motorId;
    config.motor.defaultSpeed = params.speed;
    config.motor.acceleration = params.acceleration;
    config.motor.deceleration = params.deceleration;
    config.motor.maxPosition = params.maxPosition;
    config.motor.minPosition = params.minPosition;

    config.pulsesPerMm = params.pulsesPerMm;
    config.maxPulses = params.maxPosition;
    config.depthLimits.safeDepthMm = params.safePosition;

    // 传递关键位置
    config.keyPositions = params.keyPositions;

    return config;
}

DrillConfig MotionConfigManager::getDrillConfig() const
{
    DrillConfig config;

    // 回转电机
    MechanismParams rotParams = getMechanismConfig(Mechanism::Pr);
    config.rotation.motorId = rotParams.motorId;
    config.rotation.defaultSpeed = rotParams.speed;
    config.rotation.acceleration = rotParams.acceleration;
    config.rotation.deceleration = rotParams.deceleration;
    config.defaultRotationSpeed = rotParams.speed;

    // 冲击电机
    MechanismParams percParams = getMechanismConfig(Mechanism::Pi);
    config.percussion.motorId = percParams.motorId;
    config.percussion.defaultSpeed = percParams.speed;
    config.percussion.acceleration = percParams.acceleration;
    config.percussion.deceleration = percParams.deceleration;
    config.defaultPercussionFreq = percParams.speed;

    return config;
}

RoboticArmConfig MotionConfigManager::getRoboticArmConfig() const
{
    RoboticArmConfig config;

    // 旋转
    MechanismParams rotParams = getMechanismConfig(Mechanism::Mr);
    config.rotation.motorId = rotParams.motorId;
    config.rotation.defaultSpeed = rotParams.speed;
    config.rotation.acceleration = rotParams.acceleration;
    config.rotation.deceleration = rotParams.deceleration;
    config.drillPositionAngle = rotParams.workPosition;
    config.storagePositionAngle = rotParams.safePosition;

    // 伸缩
    MechanismParams extParams = getMechanismConfig(Mechanism::Me);
    config.extension.motorId = extParams.motorId;
    config.extension.defaultSpeed = extParams.speed;
    config.extension.acceleration = extParams.acceleration;
    config.extension.deceleration = extParams.deceleration;
    config.extendPosition = extParams.workPosition;
    config.retractPosition = extParams.safePosition;

    // 夹爪
    MechanismParams clampParams = getMechanismConfig(Mechanism::Mg);
    config.clamp.motorId = clampParams.motorId;
    config.clamp.defaultSpeed = clampParams.speed;
    config.clampOpenDAC = clampParams.openDac;
    config.clampCloseDAC = clampParams.closeDac;

    return config;
}

StorageConfig MotionConfigManager::getStorageConfig() const
{
    StorageConfig config;
    MechanismParams params = getMechanismConfig(Mechanism::Sr);

    config.motor.motorId = params.motorId;
    config.motor.defaultSpeed = params.speed;
    config.motor.acceleration = params.acceleration;
    config.motor.deceleration = params.deceleration;
    config.positions = params.positions;
    config.anglePerPosition = params.anglePerPosition;

    // 传递关键位置
    config.keyPositions = params.keyPositions;

    return config;
}

ClampConfig MotionConfigManager::getClampConfig() const
{
    ClampConfig config;
    MechanismParams params = getMechanismConfig(Mechanism::Cb);

    config.motor.motorId = params.motorId;
    config.motor.defaultSpeed = params.speed;
    config.motor.acceleration = params.acceleration;
    config.motor.deceleration = params.deceleration;
    config.openDAC = params.openDac;
    config.closeDAC = params.closeDac;
    config.positionTolerance = params.positionTolerance;
    config.stableCount = params.stableCount;

    // 传递关键位置
    config.keyPositions = params.keyPositions;

    return config;
}

// ============================================================================
// 新控制器配置获取接口
// ============================================================================

// FeedController使用 getPenetrationConfig()

RotationConfig MotionConfigManager::getRotationConfig() const
{
    RotationConfig config;
    MechanismParams params = getMechanismConfig(Mechanism::Pr);

    config.motor.motorId = params.motorId;
    config.motor.defaultSpeed = params.speed;
    config.motor.acceleration = params.acceleration;
    config.motor.deceleration = params.deceleration;

    config.defaultSpeed = params.speed;
    config.maxTorque = params.closeDac;
    config.minTorque = params.openDac;

    // 传递关键位置
    config.keyPositions = params.keyPositions;

    return config;
}

PercussionConfig MotionConfigManager::getPercussionConfig() const
{
    PercussionConfig config;
    MechanismParams params = getMechanismConfig(Mechanism::Pi);

    config.motor.motorId = params.motorId;
    config.motor.defaultSpeed = params.speed;
    config.motor.acceleration = params.acceleration;
    config.motor.deceleration = params.deceleration;

    config.defaultFrequency = params.speed;
    config.unlockDAC = params.initDac;
    config.stableTime = params.monitorInterval * params.stableCount;
    config.positionTolerance = params.positionTolerance;

    // 传递关键位置
    config.keyPositions = params.keyPositions;

    return config;
}

ArmExtensionConfig MotionConfigManager::getArmExtensionConfig() const
{
    ArmExtensionConfig config;
    MechanismParams params = getMechanismConfig(Mechanism::Me);

    config.motor.motorId = params.motorId;
    config.motor.defaultSpeed = params.speed;
    config.motor.acceleration = params.acceleration;
    config.motor.deceleration = params.deceleration;

    config.extendPosition = params.workPosition;
    config.retractPosition = params.safePosition;
    config.initDAC = params.initDac;
    config.stableThreshold = params.stableThreshold;
    config.stableCount = params.stableCount;
    config.monitorInterval = params.monitorInterval;

    // 传递关键位置
    config.keyPositions = params.keyPositions;

    return config;
}

ArmGripConfig MotionConfigManager::getArmGripConfig() const
{
    ArmGripConfig config;
    MechanismParams params = getMechanismConfig(Mechanism::Mg);

    config.motor.motorId = params.motorId;
    config.motor.defaultSpeed = params.speed;
    config.motor.acceleration = params.acceleration;
    config.motor.deceleration = params.deceleration;

    config.openDAC = params.openDac;
    config.closeDAC = params.closeDac;
    config.initDAC = params.initDac;
    config.maxDAC = params.closeDac;
    config.dacIncrement = params.dacIncrement;
    config.stableThreshold = params.stableThreshold;
    config.stableCount = params.stableCount;
    config.monitorInterval = params.monitorInterval;

    // 传递关键位置
    config.keyPositions = params.keyPositions;

    return config;
}

ArmRotationConfig MotionConfigManager::getArmRotationConfig() const
{
    ArmRotationConfig config;
    MechanismParams params = getMechanismConfig(Mechanism::Mr);

    config.motor.motorId = params.motorId;
    config.motor.defaultSpeed = params.speed;
    config.motor.acceleration = params.acceleration;
    config.motor.deceleration = params.deceleration;

    config.drillPositionAngle = params.workPosition;
    config.storagePositionAngle = params.safePosition;
    config.pulsesPerDegree = params.pulsesPerDegree;
    config.positionTolerance = params.positionTolerance;

    // 传递关键位置
    config.keyPositions = params.keyPositions;

    return config;
}

DockingConfig MotionConfigManager::getDockingConfig() const
{
    DockingConfig config;
    MechanismParams params = getMechanismConfig(Mechanism::Dh);

    config.serverAddress = "192.168.1.201";  // 可从params中扩展
    config.serverPort = 502;
    config.slaveId = params.slaveId;

    config.extendCommand = 1;
    config.retractCommand = 2;
    config.stopCommand = 0;

    config.moveTimeout = params.monitorInterval * 100;
    config.statusPollInterval = params.monitorInterval;

    // 传递关键位置
    config.keyPositions = params.keyPositions;

    return config;
}
