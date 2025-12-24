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
                                       double pressureN,
                                       double forceUpperN,
                                       double forceLowerN)
{
    if (!m_armed || m_faultActive) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    // Record position for stall detection
    PositionSample posSample;
    posSample.positionMm = positionMm;
    posSample.timestampMs = nowMs;
    m_positionHistory.enqueue(posSample);

    // Record velocity for change rate detection
    VelocitySample velSample;
    velSample.velocityMmPerMin = velocityMmPerMin;
    velSample.timestampMs = nowMs;
    m_velocityHistory.enqueue(velSample);

    pruneHistory(nowMs);

    // Check emergency force limit (highest priority)
    if (m_activePreset.emergencyForceLimit > 0.0) {
        if (forceUpperN > m_activePreset.emergencyForceLimit ||
            forceLowerN > m_activePreset.emergencyForceLimit) {
            raiseFault("EMERGENCY_FORCE",
                       QString("Emergency force limit exceeded: Upper=%1N Lower=%2N (Limit=%3N)")
                           .arg(forceUpperN, 0, 'f', 1)
                           .arg(forceLowerN, 0, 'f', 1)
                           .arg(m_activePreset.emergencyForceLimit, 0, 'f', 1));
            return;
        }
    }

    // Check force upper limit
    if (m_activePreset.upperForceLimit > 0.0 && forceUpperN > m_activePreset.upperForceLimit) {
        raiseFault("FORCE_UPPER_LIMIT",
                   QString("Upper force %1 N exceeds limit %2 N")
                       .arg(forceUpperN, 0, 'f', 1)
                       .arg(m_activePreset.upperForceLimit, 0, 'f', 1));
        return;
    }

    // Check force lower limit (only during active motion to avoid startup/positioning false positives)
    if (m_activePreset.lowerForceLimit > 0.0 &&
        std::abs(velocityMmPerMin) > 1.0 &&  // Only check when actively moving (>1 mm/min)
        forceLowerN > 0.1 &&  // Ensure sensor is active
        forceLowerN < m_activePreset.lowerForceLimit) {
        raiseFault("FORCE_LOWER_LIMIT",
                   QString("Lower force %1 N below minimum %2 N during motion")
                       .arg(forceLowerN, 0, 'f', 1)
                       .arg(m_activePreset.lowerForceLimit, 0, 'f', 1));
        return;
    }

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

    // Check max feed speed
    if (m_activePreset.maxFeedSpeedMmPerMin > 0.0) {
        double absVelocity = std::abs(velocityMmPerMin);
        if (absVelocity > m_activePreset.maxFeedSpeedMmPerMin) {
            raiseFault("MAX_FEED_SPEED",
                       QString("Feed speed %1 mm/min exceeds limit %2 mm/min")
                           .arg(absVelocity, 0, 'f', 1)
                           .arg(m_activePreset.maxFeedSpeedMmPerMin, 0, 'f', 1));
            return;
        }
    }

    // Check velocity change rate
    evaluateVelocityChangeRate(velocityMmPerMin, nowMs);

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

void SafetyWatchdog::evaluateVelocityChangeRate(double velocityMmPerMin, qint64 nowMs)
{
    if (m_activePreset.velocityChangeLimitMmPerSec <= 0.0 ||
        m_velocityHistory.size() < 2) {
        return;
    }

    const VelocitySample& oldest = m_velocityHistory.head();
    const VelocitySample& latest = m_velocityHistory.last();

    // Check if we have valid time window
    const qint64 timeDeltaMs = nowMs - oldest.timestampMs;
    if (timeDeltaMs < 50) {  // Require at least 50ms for meaningful calculation
        return;
    }

    // Skip check if data is stale (e.g., telemetry stopped)
    if (timeDeltaMs > 2 * kVelocityWindowMs) {
        return;  // Data too old, skip acceleration check
    }

    // Calculate velocity change rate (acceleration)
    // Convert to mm/s² from mm/min over milliseconds
    const double velocityDelta = std::abs(latest.velocityMmPerMin - oldest.velocityMmPerMin);
    const double timeDeltaSec = timeDeltaMs / 1000.0;
    const double velocityChangeRate = (velocityDelta / 60.0) / timeDeltaSec;  // mm/min to mm/s, then per second

    if (velocityChangeRate > m_activePreset.velocityChangeLimitMmPerSec) {
        raiseFault("VELOCITY_CHANGE_RATE",
                   QString("Velocity change rate %1 mm/s² exceeds limit %2 mm/s²")
                       .arg(velocityChangeRate, 0, 'f', 2)
                       .arg(m_activePreset.velocityChangeLimitMmPerSec, 0, 'f', 2));
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
    // Prune position history (for stall detection)
    if (!m_positionHistory.isEmpty() && m_activePreset.stallWindowMs > 0) {
        while (m_positionHistory.size() > 2 &&
               (nowMs - m_positionHistory.head().timestampMs) > m_activePreset.stallWindowMs) {
            m_positionHistory.dequeue();
        }
    }

    // Prune velocity history (for velocity change rate detection)
    // Keep at least 2 samples for meaningful calculation
    if (!m_velocityHistory.isEmpty()) {
        while (m_velocityHistory.size() > 2 &&
               (nowMs - m_velocityHistory.head().timestampMs) > kVelocityWindowMs) {
            m_velocityHistory.dequeue();
        }
    }

    // Limit maximum queue size to prevent unbounded growth
    while (m_positionHistory.size() > 100) {
        m_positionHistory.dequeue();
    }
    while (m_velocityHistory.size() > 100) {
        m_velocityHistory.dequeue();
    }
}

void SafetyWatchdog::resetState()
{
    m_faultActive = false;
    m_lastFaultCode.clear();
    m_lastFaultDetail.clear();
    m_positionHistory.clear();
    m_velocityHistory.clear();
}
