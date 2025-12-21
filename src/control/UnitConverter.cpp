#include "control/UnitConverter.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace {
AxisUnitInfo buildAxisInfo(Mechanism::Code code, const MechanismParams& params)
{
    AxisUnitInfo info;
    info.code = Mechanism::getCodeString(code);
    info.motorIndex = Mechanism::getMotorIndex(code);
    if (params.pulsesPerMm > 0.0) {
        info.unitLabel = "mm";
        info.pulsesPerUnit = params.pulsesPerMm;
    } else if (params.pulsesPerDegree > 0.0) {
        info.unitLabel = "deg";
        info.pulsesPerUnit = params.pulsesPerDegree;
    }
    return info;
}

QString normalizeHeader(const QString& text)
{
    return text.trimmed().toLower();
}
} // namespace

QMap<int, AxisUnitInfo> UnitConverter::loadAxisUnits(const QMap<Mechanism::Code, MechanismParams>& configs,
                                                     const QString& csvPath,
                                                     QStringList* warnings)
{
    QMap<int, AxisUnitInfo> map;
    for (auto it = configs.constBegin(); it != configs.constEnd(); ++it) {
        AxisUnitInfo info = buildAxisInfo(it.key(), it.value());
        if (info.motorIndex >= 0) {
            map.insert(info.motorIndex, info);
        }
    }

    if (csvPath.isEmpty() || !QFileInfo::exists(csvPath)) {
        return map;
    }

    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (warnings) {
            warnings->append(QString("Failed to open unit CSV: %1").arg(csvPath));
        }
        return map;
    }

    QTextStream in(&file);
    int lineNo = 0;
    int codeIdx = 0;
    int motorIdx = 1;
    int labelIdx = 2;
    int pulsesIdx = 3;
    bool hasHeader = false;

    while (!in.atEnd()) {
        QString line = in.readLine();
        ++lineNo;
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) {
            continue;
        }

        QStringList cols = trimmed.split(',', Qt::KeepEmptyParts);
        if (!hasHeader) {
            const QString header = normalizeHeader(cols.value(0));
            if (header == "code" || header.contains("motor") || header.contains("unit")) {
                for (int i = 0; i < cols.size(); ++i) {
                    QString key = normalizeHeader(cols[i]);
                    if (key == "code") codeIdx = i;
                    else if (key == "motor_index" || key == "motor") motorIdx = i;
                    else if (key == "unit_label") labelIdx = i;
                    else if (key == "pulses_per_unit") pulsesIdx = i;
                }
                hasHeader = true;
                continue;
            }
        }
        hasHeader = true;

        QString code = cols.value(codeIdx).trimmed();
        QString motorStr = cols.value(motorIdx).trimmed();
        QString label = cols.value(labelIdx).trimmed();
        QString pulsesStr = cols.value(pulsesIdx).trimmed();

        bool okPulse = false;
        double pulsesPerUnit = pulsesStr.toDouble(&okPulse);
        if (!okPulse || pulsesPerUnit <= 0.0) {
            continue;
        }

        bool okMotor = false;
        int motorIndex = motorStr.toInt(&okMotor);
        if (!okMotor) {
            Mechanism::Code mechCode = Mechanism::fromCodeString(code);
            motorIndex = Mechanism::getMotorIndex(mechCode);
            okMotor = (motorIndex >= 0);
        }

        if (!okMotor) {
            if (warnings) {
                warnings->append(QString("Line %1: missing motor_index for code '%2'").arg(lineNo).arg(code));
            }
            continue;
        }

        AxisUnitInfo info = map.value(motorIndex);
        info.motorIndex = motorIndex;
        if (!code.isEmpty()) {
            info.code = code;
        }
        if (!label.isEmpty()) {
            info.unitLabel = label;
        }
        info.pulsesPerUnit = pulsesPerUnit;
        map.insert(motorIndex, info);
    }

    return map;
}

double UnitConverter::driverToPhysical(double driverValue, const AxisUnitInfo& axis, UnitValueType type)
{
    if (!axis.valid()) {
        return driverValue;
    }
    switch (type) {
    case UnitValueType::Position:
        return driverValue / axis.pulsesPerUnit;
    case UnitValueType::Speed:
        return driverValue / axis.pulsesPerUnit * 60.0;
    case UnitValueType::Acceleration:
        return driverValue / axis.pulsesPerUnit * 3600.0;
    default:
        return driverValue;
    }
}

double UnitConverter::physicalToDriver(double physicalValue, const AxisUnitInfo& axis, UnitValueType type)
{
    if (!axis.valid()) {
        return physicalValue;
    }
    switch (type) {
    case UnitValueType::Position:
        return physicalValue * axis.pulsesPerUnit;
    case UnitValueType::Speed:
        return physicalValue / 60.0 * axis.pulsesPerUnit;
    case UnitValueType::Acceleration:
        return physicalValue / 3600.0 * axis.pulsesPerUnit;
    default:
        return physicalValue;
    }
}
