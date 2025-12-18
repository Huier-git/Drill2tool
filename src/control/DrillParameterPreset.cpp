#include "control/DrillParameterPreset.h"

bool DrillParameterPreset::isValid() const
{
    return !id.trimmed().isEmpty()
        && feedSpeedMmPerMin > 0.0
        && rotationRpm > 0.0;
}

DrillParameterPreset DrillParameterPreset::fromJson(const QJsonObject& json)
{
    DrillParameterPreset preset;

    preset.id = json.value("id").toString();
    preset.description = json.value("description").toString();

    // Drilling parameters - support both naming conventions
    preset.feedSpeedMmPerMin = json.contains("vp_mm_per_min")
        ? json.value("vp_mm_per_min").toDouble()
        : json.value("feed_speed_mm_per_min").toDouble(30.0);

    preset.rotationRpm = json.contains("rpm")
        ? json.value("rpm").toDouble()
        : json.value("rotation_rpm").toDouble(60.0);

    preset.impactFrequencyHz = json.contains("fi_hz")
        ? json.value("fi_hz").toDouble()
        : json.value("impact_frequency_hz").toDouble(5.0);

    // Safety thresholds
    preset.torqueLimitNm = json.contains("torque_limit_nm")
        ? json.value("torque_limit_nm").toDouble()
        : json.value("torque_limit").toDouble(1600.0);

    preset.pressureLimitN = json.contains("pressure_limit_n")
        ? json.value("pressure_limit_n").toDouble()
        : json.value("pressure_limit").toDouble(15000.0);

    preset.drillStringWeightN = json.value("drill_string_weight_n").toDouble(500.0);

    preset.stallVelocityMmPerMin = json.value("stall_velocity_mm_per_min").toDouble(5.0);
    preset.stallWindowMs = json.value("stall_window_ms").toInt(1000);

    return preset;
}

QJsonObject DrillParameterPreset::toJson() const
{
    QJsonObject json;

    json["id"] = id;
    json["description"] = description;
    json["vp_mm_per_min"] = feedSpeedMmPerMin;
    json["rpm"] = rotationRpm;
    json["fi_hz"] = impactFrequencyHz;
    json["torque_limit_nm"] = torqueLimitNm;
    json["pressure_limit_n"] = pressureLimitN;
    json["drill_string_weight_n"] = drillStringWeightN;
    json["stall_velocity_mm_per_min"] = stallVelocityMmPerMin;
    json["stall_window_ms"] = stallWindowMs;

    return json;
}

DrillParameterPreset DrillParameterPreset::createDefault(const QString& id)
{
    DrillParameterPreset preset;
    preset.id = id;

    if (id == "P1") {
        preset.description = "Soft formation";
        preset.feedSpeedMmPerMin = 45.0;
        preset.rotationRpm = 60;
        preset.impactFrequencyHz = 4.5;
        preset.torqueLimitNm = 1200;
        preset.pressureLimitN = 13500;
        preset.drillStringWeightN = 500;
    } else if (id == "P2") {
        preset.description = "Standard formation";
        preset.feedSpeedMmPerMin = 38.0;
        preset.rotationRpm = 55;
        preset.impactFrequencyHz = 5.0;
        preset.torqueLimitNm = 1600;
        preset.pressureLimitN = 15000;
        preset.drillStringWeightN = 500;
    } else if (id == "P3") {
        preset.description = "Hard formation";
        preset.feedSpeedMmPerMin = 25.0;
        preset.rotationRpm = 45;
        preset.impactFrequencyHz = 6.0;
        preset.torqueLimitNm = 2000;
        preset.pressureLimitN = 18000;
        preset.drillStringWeightN = 500;
    } else if (id == "P4") {
        preset.description = "Deep drilling";
        preset.feedSpeedMmPerMin = 30.0;
        preset.rotationRpm = 50;
        preset.impactFrequencyHz = 5.5;
        preset.torqueLimitNm = 1800;
        preset.pressureLimitN = 16000;
        preset.drillStringWeightN = 800;
    } else if (id == "P5") {
        preset.description = "High speed";
        preset.feedSpeedMmPerMin = 50.0;
        preset.rotationRpm = 70;
        preset.impactFrequencyHz = 4.0;
        preset.torqueLimitNm = 1400;
        preset.pressureLimitN = 14000;
        preset.drillStringWeightN = 500;
    } else {
        preset.description = "Custom";
        preset.feedSpeedMmPerMin = 35.0;
        preset.rotationRpm = 55;
        preset.impactFrequencyHz = 5.0;
        preset.torqueLimitNm = 1600;
        preset.pressureLimitN = 15000;
        preset.drillStringWeightN = 500;
    }

    preset.stallVelocityMmPerMin = 5.0;
    preset.stallWindowMs = 1000;

    return preset;
}
