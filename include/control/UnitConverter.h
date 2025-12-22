#ifndef UNITCONVERTER_H
#define UNITCONVERTER_H

#include <QMap>
#include <QString>
#include <QStringList>

#include "control/MechanismDefs.h"

struct MechanismParams;

enum class UnitValueType {
    Position,
    Speed,
    Acceleration
};

struct AxisUnitInfo {
    QString code;
    int motorIndex = -1;
    QString unitLabel;
    double pulsesPerUnit = 0.0;

    bool valid() const { return pulsesPerUnit > 0.0; }
};

class UnitConverter
{
public:
    static QMap<int, AxisUnitInfo> loadAxisUnits(const QMap<Mechanism::Code, MechanismParams>& configs,
                                                 const QString& csvPath,
                                                 QStringList* warnings = nullptr);
    static double driverToPhysical(double driverValue, const AxisUnitInfo& axis, UnitValueType type);
    static double physicalToDriver(double physicalValue, const AxisUnitInfo& axis, UnitValueType type);
};

#endif // UNITCONVERTER_H
