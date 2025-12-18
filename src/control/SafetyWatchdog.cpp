#include "control/SafetyWatchdog.h"

#include <QDateTime>
#include <cmath>

SafetyWatchdog::SafetyWatchdog(QObject* parent)
    : QObject(parent)
{
}

void SafetyWatchdog::arm(const DrillParameterPreset& preset)
{
    m_activePreset = preset;
    resetState();
    m_armed = m_activePreset.isValid();

    if (m_armed) {
        emit armed();
    }
}

void SafetyWatchdog::disarm()
{
    m_armed = false;
    m_activePreset = DrillParameterPreset();
    resetState();
    emit disarmed();
}

void SafetyWatchdog::clearFault()
{
    m_faultActive = false;
    m_lastFaultCode.clear();
    m_lastFaultDetail.clear();
}

void SafetyWatchdog::onTelemetryUpdate(double positionMm,
                                       double velocityMmPerMin,
                                       double torqueNm,
                                       double pressureN)
{
    if (!m_armed || m_faultActive) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    // Record position for stall detection
    PositionSample sample;
    sample.positionMm = positionMm;
    sample.timestampMs = nowMs;
    m_positionHistory.enqueue(sample);
    pruneHistory(nowMs);

    // Check torque limit
    if (m_activePreset.torqueLimitNm > 0.0 &&
        torqueNm > m_activePreset.torqueLimitNm) {
        raiseFault("TORQUE_LIMIT",
                   QString("Torque %1 Nm exceeds limit %2 Nm")
                       .arg(torqueNm, 0, 'f', 1)
                       .arg(m_activePreset.torqueLimitNm, 0, 'f', 1));
        return;
    }

    // Check pressure limit
    if (m_activePreset.pressureLimitN > 0.0 &&
        pressureN > m_activePreset.pressureLimitN) {
        raiseFault("PRESSURE_LIMIT",
                   QString("Pressure %1 N exceeds limit %2 N")
                       .arg(pressureN, 0, 'f', 0)
                       .arg(m_activePreset.pressureLimitN, 0, 'f', 0));
        return;
    }

    // Check stall condition
    evaluateStallCondition(velocityMmPerMin, nowMs);
}

void SafetyWatchdog::evaluateStallCondition(double velocityMmPerMin, qint64 nowMs)
{
    if (m_activePreset.stallWindowMs <= 0 ||
        m_activePreset.stallVelocityMmPerMin <= 0.0 ||
        m_positionHistory.size() < 2) {
        return;
    }

    const PositionSample& oldest = m_positionHistory.head();
    const PositionSample& latest = m_positionHistory.last();

    // Check if we have enough time window
    const bool windowSatisfied = (nowMs - oldest.timestampMs) >= m_activePreset.stallWindowMs;
    if (!windowSatisfied) {
        return;
    }

    // Check position stability
    const double positionDelta = std::abs(latest.positionMm - oldest.positionMm);
    const bool positionStable = positionDelta <= kPositionStabilityToleranceMm;

    // Check velocity
    const bool velocityLow = std::abs(velocityMmPerMin) <= m_activePreset.stallVelocityMmPerMin;

    if (positionStable && velocityLow) {
        raiseFault("STALL_DETECTED",
                   QString("Feed stall: position change %1 mm in %2 ms")
                       .arg(positionDelta, 0, 'f', 3)
                       .arg(m_activePreset.stallWindowMs));
    }
}

void SafetyWatchdog::raiseFault(const QString& code, const QString& detail)
{
    if (m_faultActive) {
        return;
    }

    m_faultActive = true;
    m_lastFaultCode = code;
    m_lastFaultDetail = detail;
    emit faultOccurred(code, detail);
}

void SafetyWatchdog::pruneHistory(qint64 nowMs)
{
    if (m_positionHistory.isEmpty()) {
        return;
    }

    if (m_activePreset.stallWindowMs <= 0) {
        while (m_positionHistory.size() > 1) {
            m_positionHistory.dequeue();
        }
        return;
    }

    // Keep only samples within the time window
    while (!m_positionHistory.isEmpty() &&
           (nowMs - m_positionHistory.head().timestampMs) > m_activePreset.stallWindowMs) {
        m_positionHistory.dequeue();
    }
}

void SafetyWatchdog::resetState()
{
    m_faultActive = false;
    m_lastFaultCode.clear();
    m_lastFaultDetail.clear();
    m_positionHistory.clear();
}
