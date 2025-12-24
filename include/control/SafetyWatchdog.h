#ifndef SAFETYWATCHDOG_H
#define SAFETYWATCHDOG_H

#include <QObject>
#include <QQueue>
#include <QString>

#include "control/DrillParameterPreset.h"

/**
 * @brief Safety watchdog for drilling operations
 *
 * Monitors telemetry data and emits faults when safety limits are exceeded:
 * - Torque overload
 * - Pressure overload
 * - Feed stall detection
 */
class SafetyWatchdog : public QObject
{
    Q_OBJECT

public:
    explicit SafetyWatchdog(QObject* parent = nullptr);

    void arm(const DrillParameterPreset& preset);
    void disarm();
    void clearFault();

    bool isArmed() const { return m_armed; }
    bool hasFault() const { return m_faultActive; }
    QString lastFaultCode() const { return m_lastFaultCode; }
    QString lastFaultDetail() const { return m_lastFaultDetail; }

    // Current limits (for UI display)
    double torqueLimit() const { return m_activePreset.torqueLimitNm; }
    double pressureLimit() const { return m_activePreset.pressureLimitN; }

public slots:
    void onTelemetryUpdate(double positionMm,
                           double velocityMmPerMin,
                           double torqueNm,
                           double pressureN,
                           double forceUpperN,
                           double forceLowerN);

signals:
    void faultOccurred(const QString& code, const QString& detail);
    void armed();
    void disarmed();

private:
    struct PositionSample {
        double positionMm = 0.0;
        qint64 timestampMs = 0;
    };

    struct VelocitySample {
        double velocityMmPerMin = 0.0;
        qint64 timestampMs = 0;
    };

    void evaluateStallCondition(double velocityMmPerMin, qint64 nowMs);
    void evaluateVelocityChangeRate(double velocityMmPerMin, qint64 nowMs);
    void raiseFault(const QString& code, const QString& detail);
    void pruneHistory(qint64 nowMs);
    void resetState();

    DrillParameterPreset m_activePreset;
    bool m_armed = false;
    bool m_faultActive = false;
    QString m_lastFaultCode;
    QString m_lastFaultDetail;
    QQueue<PositionSample> m_positionHistory;
    QQueue<VelocitySample> m_velocityHistory;

    static constexpr double kPositionStabilityToleranceMm = 0.05;
    static constexpr qint64 kVelocityWindowMs = 500;  // 500ms window for velocity change detection
};

#endif // SAFETYWATCHDOG_H
