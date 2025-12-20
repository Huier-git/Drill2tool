#include "Logger.h"
#include "control/AutoDrillManager.h"

#include "control/FeedController.h"
#include "control/RotationController.h"
#include "control/PercussionController.h"
#include "control/SafetyWatchdog.h"
#include "database/DbWriter.h"
#include <QMetaObject>
#include "control/MotionLockManager.h"
#include "control/MechanismTypes.h"
#include "dataACQ/MdbWorker.h"
#include "dataACQ/MotorWorker.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTimer>
#include <QDateTime>
#include <QDebug>
#include <cmath>

// TaskStep implementation
bool TaskStep::isValid() const
{
    if (type == Type::Hold) {
        return holdTimeSec > 0;
    }

    return std::isfinite(targetDepthMm) && !presetId.trimmed().isEmpty();
}

QString TaskStep::typeToString(TaskStep::Type type)
{
    switch (type) {
    case Type::Drilling:
        return "drilling";
    case Type::Hold:
        return "hold";
    default:
        return "positioning";
    }
}

TaskStep::Type TaskStep::typeFromString(const QString& typeStr)
{
    QString normalized = typeStr.trimmed().toLower();
    if (normalized == "drilling") {
        return Type::Drilling;
    }
    if (normalized == "hold") {
        return Type::Hold;
    }
    return Type::Positioning;
}

TaskStep TaskStep::fromJson(const QJsonObject& json)
{
    TaskStep step;
    step.type = typeFromString(json.value("type").toString("positioning"));
    step.targetDepthMm = json.value("target_depth").toDouble(0.0);
    step.presetId = json.value("param_id").toString();
    step.timeoutSec = json.value("timeout").toInt(0);
    step.holdTimeSec = json.value("hold_time").toInt(0);

    if (json.contains("conditions") && json.value("conditions").isObject()) {
        step.conditions = json.value("conditions").toObject();
    }

    return step;
}

QJsonObject TaskStep::toJson() const
{
    QJsonObject json;
    json["type"] = typeToString(type);
    json["target_depth"] = targetDepthMm;
    json["param_id"] = presetId;
    json["timeout"] = timeoutSec;
    json["hold_time"] = holdTimeSec;

    if (!conditions.isEmpty()) {
        json["conditions"] = conditions;
    }

    return json;
}

// AutoDrillManager implementation
AutoDrillManager::AutoDrillManager(FeedController* feed,
                                   RotationController* rotation,
                                   PercussionController* percussion,
                                   QObject* parent)
    : QObject(parent)
    , m_state(AutoTaskState::Idle)
    , m_currentStepIndex(-1)
    , m_stepExecutionState(StepExecutionState::Pending)
    , m_motionLockAcquired(false)
    , m_pauseRequested(false)
    , m_abortRequested(false)
    , m_feed(feed)
    , m_rotation(rotation)
    , m_percussion(percussion)
    , m_watchdog(new SafetyWatchdog(this))
    , m_dbWriter(nullptr)
    , m_mdbWorker(nullptr)
    , m_motorWorker(nullptr)
    , m_stepTimeoutTimer(new QTimer(this))
    , m_holdTimer(new QTimer(this))
    , m_lastDepthMm(0.0)
    , m_lastVelocityMmPerMin(0.0)
    , m_lastTorqueNm(0.0)
    , m_lastForceUpperN(0.0)
    , m_lastForceLowerN(0.0)
    , m_lastPressureN(0.0)
    , m_lastStallDetected(false)
    , m_hasActivePreset(false)
    , m_totalTargetDepth(0.0)
    , m_roundId(0)
{
    m_stepTimeoutTimer->setSingleShot(true);
    m_holdTimer->setSingleShot(true);

    connect(m_stepTimeoutTimer, &QTimer::timeout,
            this, &AutoDrillManager::onStepTimeout);
    connect(m_holdTimer, &QTimer::timeout,
            this, &AutoDrillManager::onHoldTimeout);
    connect(m_watchdog, &SafetyWatchdog::faultOccurred,
            this, &AutoDrillManager::onWatchdogFault);

    if (m_feed) {
        connect(m_feed, &FeedController::targetReached,
                this, &AutoDrillManager::onFeedTargetReached);
        connect(m_feed, &FeedController::stateChanged,
                this, &AutoDrillManager::onFeedStateChanged);
    }
}

AutoDrillManager::~AutoDrillManager()
{
    stopAllControllers();
    releaseMotionLock();
}

bool AutoDrillManager::loadTaskFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QString error = tr("无法打开任务文件: %1").arg(filePath);
        emit logMessage(error);
        emit taskFailed(error);
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        QString error = tr("任务文件解析错误: %1").arg(parseError.errorString());
        emit logMessage(error);
        emit taskFailed(error);
        return false;
    }

    if (!document.isObject()) {
        QString error = tr("任务文件格式错误: 根节点必须是JSON对象");
        emit logMessage(error);
        emit taskFailed(error);
        return false;
    }

    clearTask();

    QJsonObject root = document.object();
    loadPresets(root);

    QJsonArray stepsArray = root.value("steps").toArray();
    if (!loadSteps(stepsArray)) {
        QString error = tr("任务文件不包含有效步骤");
        emit logMessage(error);
        emit taskFailed(error);
        return false;
    }

    m_taskFilePath = filePath;
    QString message = tr("任务已加载: %1 个步骤").arg(m_steps.size());
    setState(AutoTaskState::Idle, message);
    emit logMessage(message);

    return true;
}

void AutoDrillManager::clearTask()
{
    stopAllControllers();

    if (m_stepTimeoutTimer) {
        m_stepTimeoutTimer->stop();
    }
    if (m_holdTimer) {
        m_holdTimer->stop();
    }

    m_steps.clear();
    m_presets.clear();
    m_currentStepIndex = -1;
    m_stepExecutionState = StepExecutionState::Pending;
    m_stateMessage.clear();
    m_taskFilePath.clear();
    m_pauseRequested = false;
    m_abortRequested = false;
    m_totalTargetDepth = 0.0;
    m_activePreset = DrillParameterPreset();
    m_hasActivePreset = false;
    m_lastDepthMm = 0.0;
    m_lastVelocityMmPerMin = 0.0;
    m_lastTorqueNm = 0.0;
    m_lastForceUpperN = 0.0;
    m_lastForceLowerN = 0.0;
    m_lastPressureN = 0.0;
    m_lastStallDetected = false;

    if (m_watchdog) {
        m_watchdog->disarm();
        m_watchdog->clearFault();
    }

    releaseMotionLock();
    setState(AutoTaskState::Idle, tr("任务已清除"));
}

bool AutoDrillManager::start()
{
    if (m_steps.isEmpty()) {
        emit logMessage(tr("无法开始: 未加载任务"));
        return false;
    }

    if (!m_feed || !m_rotation || !m_percussion) {
        emit logMessage(tr("无法开始: 机构控制器未就绪"));
        return false;
    }

    // 检查传感器数据连接
    if (!hasSensorData()) {
        emit logMessage(tr("无法开始: 传感器数据未连接，安全监控无法工作"));
        return false;
    }

    if (!m_motionLockAcquired && !acquireMotionLock(tr("自动钻进任务"))) {
        emit logMessage(tr("无法开始: 运动锁定失败"));
        return false;
    }

    m_abortRequested = false;
    m_pauseRequested = false;
    m_currentStepIndex = -1;
    m_stepExecutionState = StepExecutionState::Pending;

    emit logMessage(tr("开始执行自动任务"));
    recordTaskEvent("started", tr("开始执行自动任务"));
    setState(AutoTaskState::Preparing, tr("准备执行任务"));
    return prepareNextStep();
}

bool AutoDrillManager::pause()
{
    if (m_state != AutoTaskState::Drilling && m_state != AutoTaskState::Moving) {
        return false;
    }

    m_pauseRequested = true;

    if (m_stepTimeoutTimer) {
        m_stepTimeoutTimer->stop();
    }
    if (m_holdTimer) {
        m_holdTimer->stop();
    }

    stopAllControllers();

    if (m_watchdog) {
        m_watchdog->disarm();
    }

    m_stepExecutionState = StepExecutionState::Pending;
    setState(AutoTaskState::Paused, tr("任务已暂停"));
    emit logMessage(tr("任务已暂停"));
    return true;
}

bool AutoDrillManager::resume()
{
    if (m_state != AutoTaskState::Paused ||
        m_currentStepIndex < 0 ||
        m_currentStepIndex >= m_steps.size()) {
        return false;
    }

    if (!m_motionLockAcquired && !acquireMotionLock(tr("恢复自动钻进任务"))) {
        emit logMessage(tr("无法恢复: 运动锁定失败"));
        return false;
    }

    m_pauseRequested = false;

    if (m_watchdog && m_hasActivePreset) {
        m_watchdog->arm(m_activePreset);
    }

    emit logMessage(tr("恢复执行任务"));
    recordTaskEvent("resumed", tr("恢复执行任务"), m_currentStepIndex);
    setState(AutoTaskState::Preparing, tr("恢复步骤 %1").arg(m_currentStepIndex + 1));

    executeStep(m_steps[m_currentStepIndex]);
    return true;
}

void AutoDrillManager::abort()
{
    if (m_state == AutoTaskState::Idle) {
        return;
    }

    m_abortRequested = true;
    emit logMessage(tr("任务被用户中止"));
    failTask(tr("任务被用户中止"));
}

void AutoDrillManager::emergencyStop()
{
    emit logMessage(tr("触发急停"));
    if (MotionLockManager* lock = MotionLockManager::instance()) {
        lock->emergencyStop();
    }
    failTask(tr("触发急停"));
}

QString AutoDrillManager::stateString() const
{
    switch (m_state) {
    case AutoTaskState::Preparing:
        return tr("准备中");
    case AutoTaskState::Moving:
        return tr("定位中");
    case AutoTaskState::Drilling:
        return tr("钻进中");
    case AutoTaskState::Paused:
        return tr("已暂停");
    case AutoTaskState::Finished:
        return tr("已完成");
    case AutoTaskState::Error:
        return tr("错误");
    case AutoTaskState::Idle:
    default:
        return tr("空闲");
    }
}

void AutoDrillManager::setDataWorkers(MdbWorker* mdbWorker, MotorWorker* motorWorker)
{
    // 断开旧连接
    if (m_mdbWorker) {
        disconnect(m_mdbWorker, nullptr, this, nullptr);
    }
    if (m_motorWorker) {
        disconnect(m_motorWorker, nullptr, this, nullptr);
    }

    m_mdbWorker = mdbWorker;
    m_motorWorker = motorWorker;

    // 建立新连接
    if (m_mdbWorker) {
        connect(m_mdbWorker, &MdbWorker::dataBlockReady,
                this, &AutoDrillManager::onDataBlockReceived);
    }
    if (m_motorWorker) {
        connect(m_motorWorker, &MotorWorker::dataBlockReady,
                this, &AutoDrillManager::onDataBlockReceived);
    }

    emit logMessage(tr("数据采集连接已建立"));
}

bool AutoDrillManager::hasSensorData() const
{
    return (m_mdbWorker != nullptr && m_motorWorker != nullptr);
}

void AutoDrillManager::onDataBlockReceived(const DataBlock& block)
{
    if (block.values.isEmpty()) {
        return;
    }

    // 提取最新值
    double latestValue = block.values.last();

    // 根据传感器类型收集原始数据
    switch (block.sensorType) {
    case SensorType::Torque_MDB:
        m_lastTorqueNm = latestValue;
        break;

    case SensorType::Force_Upper:
        m_lastForceUpperN = latestValue;
        break;

    case SensorType::Force_Lower:
        m_lastForceLowerN = latestValue;
        break;

    case SensorType::Motor_Position:
        m_lastDepthMm = latestValue;
        break;

    case SensorType::Motor_Speed:
        m_lastVelocityMmPerMin = latestValue;
        break;

    default:
        return;  // 忽略其他传感器
    }

    // 计算钻压：Pressure = 2*(Force_Upper - Force_Lower) - G
    // G 从当前激活的预设中获取
    double drillStringWeight = (m_hasActivePreset && m_activePreset.drillStringWeightN > 0.0)
        ? m_activePreset.drillStringWeightN
        : 500.0;  // 默认值

    m_lastPressureN = 2.0 * (m_lastForceUpperN - m_lastForceLowerN) - drillStringWeight;

    // 更新堵转检测
    double stallThreshold = (m_hasActivePreset && m_activePreset.stallVelocityMmPerMin > 0.0)
        ? m_activePreset.stallVelocityMmPerMin
        : 0.5;
    m_lastStallDetected = std::abs(m_lastVelocityMmPerMin) <= stallThreshold;

    // 转发给安全看门狗
    if (m_watchdog) {
        m_watchdog->onTelemetryUpdate(m_lastDepthMm, m_lastVelocityMmPerMin,
                                      m_lastTorqueNm, m_lastPressureN);
    }

    // 更新进度（仅在运动时）
    if (m_state == AutoTaskState::Moving || m_state == AutoTaskState::Drilling) {
        emit progressUpdated(m_lastDepthMm, computeProgressPercent(m_lastDepthMm));
    }

    // 检查条件停止
    if (m_stepExecutionState == StepExecutionState::InProgress &&
        m_currentStepIndex >= 0 &&
        m_currentStepIndex < m_steps.size() &&
        evaluateConditions(m_steps[m_currentStepIndex])) {
        emit logMessage(tr("条件满足，完成步骤 %1").arg(m_currentStepIndex + 1));
        completeCurrentStep();
    }
}

void AutoDrillManager::onFeedTargetReached()
{
    if (m_state == AutoTaskState::Error ||
        m_state == AutoTaskState::Paused ||
        m_stepExecutionState != StepExecutionState::InProgress) {
        return;
    }

    if (m_currentStepIndex < 0 || m_currentStepIndex >= m_steps.size()) {
        return;
    }

    const TaskStep& step = m_steps[m_currentStepIndex];
    if (!step.requiresMotion()) {
        return;
    }

    emit logMessage(tr("到达目标深度"));
    completeCurrentStep();
}

void AutoDrillManager::onFeedStateChanged(MechanismState state, const QString& msg)
{
    Q_UNUSED(state);
    Q_UNUSED(msg);
    // 可以在此处理进给状态变化
}

void AutoDrillManager::onWatchdogFault(const QString& code, const QString& detail)
{
    QString error = tr("安全故障 %1: %2").arg(code, detail);
    emit logMessage(error);
    failTask(error);
}

void AutoDrillManager::onStepTimeout()
{
    if (m_state == AutoTaskState::Error ||
        m_stepExecutionState != StepExecutionState::InProgress) {
        return;
    }

    QString reason = tr("步骤超时");
    if (m_currentStepIndex >= 0 && m_currentStepIndex < m_steps.size()) {
        reason = tr("步骤 %1 超时 (%2 秒)")
                     .arg(m_currentStepIndex + 1)
                     .arg(m_steps[m_currentStepIndex].timeoutSec);
    }
    emit logMessage(reason);
    failTask(reason);
}

void AutoDrillManager::onHoldTimeout()
{
    if (m_stepExecutionState == StepExecutionState::InProgress) {
        emit logMessage(tr("保持时间结束"));
        completeCurrentStep();
    }
}

void AutoDrillManager::setState(AutoTaskState newState, const QString& message)
{
    if (m_state == newState && m_stateMessage == message) {
        return;
    }

    m_state = newState;
    m_stateMessage = message;
    emit stateChanged(newState, message);
}

bool AutoDrillManager::acquireMotionLock(const QString& reason)
{
    if (m_motionLockAcquired) {
        return true;
    }

    if (MotionLockManager* lock = MotionLockManager::instance()) {
        if (lock->requestMotion(MotionSource::AutoScript, reason)) {
            m_motionLockAcquired = true;
            return true;
        }
    }

    return false;
}

void AutoDrillManager::releaseMotionLock()
{
    if (!m_motionLockAcquired) {
        return;
    }

    if (MotionLockManager* lock = MotionLockManager::instance()) {
        lock->releaseMotion(MotionSource::AutoScript);
    }
    m_motionLockAcquired = false;
}

bool AutoDrillManager::prepareNextStep()
{
    if (m_abortRequested) {
        failTask(tr("任务已中止"));
        return false;
    }

    ++m_currentStepIndex;

    if (m_currentStepIndex >= m_steps.size()) {
        stopAllControllers();
        if (m_watchdog) {
            m_watchdog->disarm();
            m_watchdog->clearFault();
        }
        releaseMotionLock();
        setState(AutoTaskState::Finished, tr("任务完成"));
        emit logMessage(tr("任务完成"));
        emit taskCompleted();
        int lastStepIndex = m_steps.isEmpty() ? -1 : m_steps.size() - 1;
        recordTaskEvent("finished", tr("任务完成"), lastStepIndex);
        return false;
    }

    m_stepExecutionState = StepExecutionState::Pending;
    const TaskStep& step = m_steps[m_currentStepIndex];
    emit stepStarted(m_currentStepIndex, step);
    emit logMessage(tr("开始步骤 %1/%2: %3")
                        .arg(m_currentStepIndex + 1)
                        .arg(m_steps.size())
                        .arg(TaskStep::typeToString(step.type)));
    recordTaskEvent("step_started", tr("进入步骤"), m_currentStepIndex);
    executeStep(step);
    return true;
}

void AutoDrillManager::executeStep(const TaskStep& step)
{
    m_stepExecutionState = StepExecutionState::InProgress;
    m_stepElapsed.restart();

    if (step.timeoutSec > 0 && m_stepTimeoutTimer) {
        m_stepTimeoutTimer->start(step.timeoutSec * 1000);
    } else if (m_stepTimeoutTimer) {
        m_stepTimeoutTimer->stop();
    }

    if (step.type == TaskStep::Type::Hold) {
        if (m_watchdog) {
            m_watchdog->disarm();
            m_watchdog->clearFault();
        }
        m_hasActivePreset = false;
        m_activePreset = DrillParameterPreset();

        int holdSeconds = qMax(1, step.holdTimeSec);
        if (m_holdTimer) {
            m_holdTimer->start(holdSeconds * 1000);
        }

        setState(AutoTaskState::Moving,
                 tr("保持位置 %1 秒").arg(holdSeconds));
        return;
    }

    DrillParameterPreset preset = m_presets.value(step.presetId);
    if (!preset.isValid()) {
        preset = DrillParameterPreset::createDefault(step.presetId);
    }

    if (!preset.isValid()) {
        failTask(tr("预设参数 '%1' 无效").arg(step.presetId));
        return;
    }

    applyPreset(preset, step.type);

    if (!m_feed || !m_feed->setTargetDepth(step.targetDepthMm, preset.feedSpeedMmPerMin)) {
        failTask(tr("无法移动到深度 %1 mm").arg(step.targetDepthMm));
        return;
    }

    QString message = (step.type == TaskStep::Type::Drilling)
        ? tr("钻进至 %1 mm (预设 %2)").arg(step.targetDepthMm).arg(preset.id)
        : tr("定位至 %1 mm (预设 %2)").arg(step.targetDepthMm).arg(preset.id);

    setState(step.type == TaskStep::Type::Drilling ? AutoTaskState::Drilling
                                                   : AutoTaskState::Moving,
             message);
}

void AutoDrillManager::completeCurrentStep()
{
    if (m_state == AutoTaskState::Error ||
        m_stepExecutionState != StepExecutionState::InProgress) {
        return;
    }

    m_stepExecutionState = StepExecutionState::Completed;

    if (m_stepTimeoutTimer) {
        m_stepTimeoutTimer->stop();
    }
    if (m_holdTimer) {
        m_holdTimer->stop();
    }

    if (m_watchdog) {
        m_watchdog->disarm();
        m_watchdog->clearFault();
    }

    m_hasActivePreset = false;
    m_activePreset = DrillParameterPreset();

    emit logMessage(tr("步骤 %1 完成").arg(m_currentStepIndex + 1));
    emit stepCompleted(m_currentStepIndex);
    recordTaskEvent("step_completed", tr("步骤完成"), m_currentStepIndex);
    prepareNextStep();
}

void AutoDrillManager::failTask(const QString& reason)
{
    if (m_state == AutoTaskState::Error) {
        return;
    }

    stopAllControllers();

    if (m_stepTimeoutTimer) {
        m_stepTimeoutTimer->stop();
    }
    if (m_holdTimer) {
        m_holdTimer->stop();
    }

    if (m_watchdog) {
        m_watchdog->disarm();
        m_watchdog->clearFault();
    }

    m_hasActivePreset = false;
    m_activePreset = DrillParameterPreset();
    m_stepExecutionState = StepExecutionState::Pending;
    releaseMotionLock();

    QString message = reason.isEmpty() ? tr("任务失败") : reason;
    setState(AutoTaskState::Error, message);
    recordTaskEvent("failed", message, m_currentStepIndex);
    emit taskFailed(message);
}

void AutoDrillManager::stopAllControllers()
{
    if (m_feed) {
        m_feed->stop();
    }
    if (m_rotation) {
        m_rotation->stopRotation();
        m_rotation->stop();
    }
    if (m_percussion) {
        m_percussion->stopPercussion();
        m_percussion->stop();
    }
}

void AutoDrillManager::applyPreset(const DrillParameterPreset& preset, TaskStep::Type type)
{
    m_activePreset = preset;
    m_hasActivePreset = preset.isValid();

    emit logMessage(tr("应用预设 %1: Vp=%2 RPM=%3 Fi=%4")
                        .arg(preset.id)
                        .arg(preset.feedSpeedMmPerMin)
                        .arg(preset.rotationRpm)
                        .arg(preset.impactFrequencyHz));

    if (m_watchdog) {
        if (m_hasActivePreset) {
            m_watchdog->arm(preset);
        } else {
            m_watchdog->disarm();
        }
    }

    if (m_rotation) {
        if (preset.rotationRpm > 0.0) {
            m_rotation->setSpeed(preset.rotationRpm);
            if (!m_rotation->isRotating()) {
                m_rotation->startRotation(preset.rotationRpm);
            }
        } else {
            m_rotation->stopRotation();
        }
    }

    if (m_percussion) {
        if (type == TaskStep::Type::Drilling && preset.impactFrequencyHz > 0.0) {
            m_percussion->setFrequency(preset.impactFrequencyHz);
            if (!m_percussion->isPercussing()) {
                m_percussion->startPercussion(preset.impactFrequencyHz);
            }
        } else {
            m_percussion->stopPercussion();
        }
    }
}

void AutoDrillManager::recordTaskEvent(const QString& state,
                                       const QString& reason,
                                       int stepIndexOverride)
{
    if (!m_dbWriter || m_roundId <= 0) {
        return;
    }

    int stepIndex = (stepIndexOverride >= 0) ? stepIndexOverride : m_currentStepIndex;

    QMetaObject::invokeMethod(
        m_dbWriter,
        "logAutoTaskEvent",
        Qt::QueuedConnection,
        Q_ARG(int, m_roundId),
        Q_ARG(QString, m_taskFilePath),
        Q_ARG(int, stepIndex),
        Q_ARG(QString, state),
        Q_ARG(QString, reason),
        Q_ARG(double, m_lastDepthMm),
        Q_ARG(double, m_lastTorqueNm),
        Q_ARG(double, m_lastPressureN),
        Q_ARG(double, m_lastVelocityMmPerMin),
        Q_ARG(double, m_lastForceUpperN),
        Q_ARG(double, m_lastForceLowerN));
}

bool AutoDrillManager::evaluateConditions(const TaskStep& step) const
{
    if (step.conditions.isEmpty()) {
        return false;
    }

    QJsonArray stopIf = step.conditions.value("stop_if").toArray();
    if (stopIf.isEmpty()) {
        return false;
    }

    QString logic = step.conditions.value("logic").toString("OR").toUpper();
    bool useOr = (logic == "OR");

    for (const QJsonValue& value : stopIf) {
        if (!value.isObject()) {
            continue;
        }

        bool conditionMet = evaluateSingleCondition(value.toObject());

        if (useOr && conditionMet) {
            return true;  // OR logic: any true returns true
        }
        if (!useOr && !conditionMet) {
            return false;  // AND logic: any false returns false
        }
    }

    return !useOr;  // AND logic: all true returns true
}

bool AutoDrillManager::evaluateSingleCondition(const QJsonObject& condition) const
{
    QString sensor = condition.value("sensor").toString();
    QString op = condition.value("op").toString();
    double value = condition.value("value").toDouble();

    double currentValue = 0.0;
    if (sensor == "torque") {
        currentValue = m_lastTorqueNm;
    } else if (sensor == "pressure") {
        currentValue = m_lastPressureN;
    } else if (sensor == "stall") {
        return m_lastStallDetected == (value > 0.5);
    } else {
        return false;
    }

    if (op == ">") {
        return currentValue > value;
    } else if (op == ">=") {
        return currentValue >= value;
    } else if (op == "<") {
        return currentValue < value;
    } else if (op == "<=") {
        return currentValue <= value;
    } else if (op == "==") {
        return qFuzzyCompare(currentValue, value);
    }

    return false;
}

double AutoDrillManager::computeProgressPercent(double depthMm) const
{
    if (m_totalTargetDepth <= 0.0) {
        return 0.0;
    }

    double percent = (depthMm / m_totalTargetDepth) * 100.0;
    return qBound(0.0, percent, 100.0);
}

bool AutoDrillManager::loadPresets(const QJsonObject& root)
{
    m_presets.clear();

    QJsonValue presetsValue = root.value("presets");

    if (presetsValue.isArray()) {
        QJsonArray presetArray = presetsValue.toArray();
        for (const QJsonValue& value : presetArray) {
            if (!value.isObject()) {
                continue;
            }
            DrillParameterPreset preset = DrillParameterPreset::fromJson(value.toObject());
            if (preset.isValid()) {
                m_presets.insert(preset.id, preset);
            }
        }
    }

    return true;
}

bool AutoDrillManager::loadSteps(const QJsonArray& array)
{
    m_steps.clear();
    m_totalTargetDepth = 0.0;

    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }

        TaskStep step = TaskStep::fromJson(value.toObject());
        if (!step.isValid()) {
            qWarning() << "[AutoDrillManager] Invalid step ignored";
            continue;
        }

        m_steps.append(step);
        if (step.targetDepthMm > m_totalTargetDepth) {
            m_totalTargetDepth = step.targetDepthMm;
        }
    }

    return !m_steps.isEmpty();
}
