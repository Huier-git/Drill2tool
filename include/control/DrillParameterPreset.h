#ifndef DRILLPARAMETERPRESET_H
#define DRILLPARAMETERPRESET_H

#include <QString>
#include <QJsonObject>
#include <QMetaType>

/**
 * @brief P1-P6 drilling parameter preset for AutoTask sequences
 */
struct DrillParameterPreset
{
    QString id;                        // "P1" - "P6"
    QString description;

    // Drilling parameters
    double feedSpeedMmPerMin = 0.0;    // Vp - feed rate
    double rotationRpm = 0.0;          // RPM - rotation speed
    double impactFrequencyHz = 0.0;    // Fi - percussion frequency

    // Safety thresholds
    double torqueLimitNm = 0.0;        // Max torque (N·m)
    double pressureLimitN = 0.0;       // Max drilling pressure (N)
    double drillStringWeightN = 0.0;   // Drill string weight G (N), increases with depth
    double stallVelocityMmPerMin = 5.0;
    int stallWindowMs = 1000;

    bool isValid() const;
    static DrillParameterPreset fromJson(const QJsonObject& json);
    QJsonObject toJson() const;

    // Default presets
    static DrillParameterPreset createDefault(const QString& id);
};

Q_DECLARE_METATYPE(DrillParameterPreset)

#endif // DRILLPARAMETERPRESET_H
