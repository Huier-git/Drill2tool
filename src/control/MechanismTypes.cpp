#include "control/MechanismTypes.h"
#include <QJsonArray>

// ============================================================================
// MotorConfig 实现
// ============================================================================

MotorConfig MotorConfig::fromJson(const QJsonObject& json) {
    MotorConfig config;
    config.motorId = json.value("motorId").toInt(-1);
    config.defaultSpeed = json.value("defaultSpeed").toDouble(100.0);
    config.acceleration = json.value("acceleration").toDouble(100.0);
    config.deceleration = json.value("deceleration").toDouble(100.0);
    config.maxSpeed = json.value("maxSpeed").toDouble(1000.0);
    config.minSpeed = json.value("minSpeed").toDouble(0.0);
    config.maxPosition = json.value("maxPosition").toDouble(1e6);
    config.minPosition = json.value("minPosition").toDouble(-1e6);
    return config;
}

QJsonObject MotorConfig::toJson() const {
    QJsonObject json;
    json["motorId"] = motorId;
    json["defaultSpeed"] = defaultSpeed;
    json["acceleration"] = acceleration;
    json["deceleration"] = deceleration;
    json["maxSpeed"] = maxSpeed;
    json["minSpeed"] = minSpeed;
    json["maxPosition"] = maxPosition;
    json["minPosition"] = minPosition;
    return json;
}

// ============================================================================
// MechanismConfig 实现
// ============================================================================

MechanismConfig MechanismConfig::fromJson(const QJsonObject& json) {
    MechanismConfig config;
    config.name = json.value("name").toString();
    config.enabled = json.value("enabled").toBool(true);
    config.initTimeout = json.value("initTimeout").toInt(10000);
    return config;
}

QJsonObject MechanismConfig::toJson() const {
    QJsonObject json;
    json["name"] = name;
    json["enabled"] = enabled;
    json["initTimeout"] = initTimeout;
    return json;
}

// ============================================================================
// RoboticArmConfig 实现
// ============================================================================

RoboticArmConfig RoboticArmConfig::fromJson(const QJsonObject& json) {
    RoboticArmConfig config;
    
    if (json.contains("rotation"))
        config.rotation = MotorConfig::fromJson(json["rotation"].toObject());
    if (json.contains("extension"))
        config.extension = MotorConfig::fromJson(json["extension"].toObject());
    if (json.contains("clamp"))
        config.clamp = MotorConfig::fromJson(json["clamp"].toObject());
    
    config.drillPositionAngle = json.value("drillPositionAngle").toDouble(0.0);
    config.storagePositionAngle = json.value("storagePositionAngle").toDouble(90.0);
    config.extendPosition = json.value("extendPosition").toDouble(200.0);
    config.retractPosition = json.value("retractPosition").toDouble(0.0);
    config.clampOpenDAC = json.value("clampOpenDAC").toDouble(-100.0);
    config.clampCloseDAC = json.value("clampCloseDAC").toDouble(100.0);
    
    return config;
}

QJsonObject RoboticArmConfig::toJson() const {
    QJsonObject json;
    json["rotation"] = rotation.toJson();
    json["extension"] = extension.toJson();
    json["clamp"] = clamp.toJson();
    json["drillPositionAngle"] = drillPositionAngle;
    json["storagePositionAngle"] = storagePositionAngle;
    json["extendPosition"] = extendPosition;
    json["retractPosition"] = retractPosition;
    json["clampOpenDAC"] = clampOpenDAC;
    json["clampCloseDAC"] = clampCloseDAC;
    return json;
}

// ============================================================================
// PenetrationConfig 实现
// ============================================================================

PenetrationConfig PenetrationConfig::fromJson(const QJsonObject& json) {
    PenetrationConfig config;
    
    if (json.contains("motor"))
        config.motor = MotorConfig::fromJson(json["motor"].toObject());
    
    if (json.contains("depthLimits")) {
        auto limits = json["depthLimits"].toObject();
        config.depthLimits.maxDepthMm = limits.value("maxDepthMm").toDouble(1059.0);
        config.depthLimits.minDepthMm = limits.value("minDepthMm").toDouble(58.0);
        config.depthLimits.safeDepthMm = limits.value("safeDepthMm").toDouble(1059.0);
    }
    
    config.pulsesPerMm = json.value("pulsesPerMm").toDouble(13086.9);
    config.maxPulses = json.value("maxPulses").toDouble(13100000.0);
    
    return config;
}

QJsonObject PenetrationConfig::toJson() const {
    QJsonObject json;
    json["motor"] = motor.toJson();
    
    QJsonObject limits;
    limits["maxDepthMm"] = depthLimits.maxDepthMm;
    limits["minDepthMm"] = depthLimits.minDepthMm;
    limits["safeDepthMm"] = depthLimits.safeDepthMm;
    json["depthLimits"] = limits;
    
    json["pulsesPerMm"] = pulsesPerMm;
    json["maxPulses"] = maxPulses;
    
    return json;
}

// ============================================================================
// DrillConfig 实现
// ============================================================================

DrillConfig DrillConfig::fromJson(const QJsonObject& json) {
    DrillConfig config;
    
    if (json.contains("rotation"))
        config.rotation = MotorConfig::fromJson(json["rotation"].toObject());
    if (json.contains("percussion"))
        config.percussion = MotorConfig::fromJson(json["percussion"].toObject());
    
    config.defaultRotationSpeed = json.value("defaultRotationSpeed").toDouble(60.0);
    config.defaultPercussionFreq = json.value("defaultPercussionFreq").toDouble(5.0);
    config.unlockDAC = json.value("unlockDAC").toDouble(-30.0);
    config.unlockPosition = json.value("unlockPosition").toDouble(-100.0);
    config.stableTime = json.value("stableTime").toInt(3000);
    config.positionTolerance = json.value("positionTolerance").toDouble(1.0);
    
    return config;
}

QJsonObject DrillConfig::toJson() const {
    QJsonObject json;
    json["rotation"] = rotation.toJson();
    json["percussion"] = percussion.toJson();
    json["defaultRotationSpeed"] = defaultRotationSpeed;
    json["defaultPercussionFreq"] = defaultPercussionFreq;
    json["unlockDAC"] = unlockDAC;
    json["unlockPosition"] = unlockPosition;
    json["stableTime"] = stableTime;
    json["positionTolerance"] = positionTolerance;
    return json;
}

// ============================================================================
// StorageConfig 实现
// ============================================================================

StorageConfig StorageConfig::fromJson(const QJsonObject& json) {
    StorageConfig config;
    
    if (json.contains("motor"))
        config.motor = MotorConfig::fromJson(json["motor"].toObject());
    
    config.positions = json.value("positions").toInt(7);
    config.anglePerPosition = json.value("anglePerPosition").toDouble(51.43);
    
    return config;
}

QJsonObject StorageConfig::toJson() const {
    QJsonObject json;
    json["motor"] = motor.toJson();
    json["positions"] = positions;
    json["anglePerPosition"] = anglePerPosition;
    return json;
}

// ============================================================================
// ClampConfig 实现
// ============================================================================

ClampConfig ClampConfig::fromJson(const QJsonObject& json) {
    ClampConfig config;
    
    if (json.contains("motor"))
        config.motor = MotorConfig::fromJson(json["motor"].toObject());
    
    config.openDAC = json.value("openDAC").toDouble(-100.0);
    config.closeDAC = json.value("closeDAC").toDouble(100.0);
    config.positionTolerance = json.value("positionTolerance").toDouble(1.0);
    config.stableCount = json.value("stableCount").toInt(5);
    
    return config;
}

QJsonObject ClampConfig::toJson() const {
    QJsonObject json;
    json["motor"] = motor.toJson();
    json["openDAC"] = openDAC;
    json["closeDAC"] = closeDAC;
    json["positionTolerance"] = positionTolerance;
    json["stableCount"] = stableCount;
    return json;
}
