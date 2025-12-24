#include "Logger.h"
#include "control/AutoDrillManager.h"

#include "control/FeedController.h"
#include "control/RotationController.h"
#include "control/PercussionController.h"
#include "control/SafetyWatchdog.h"
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
        // Hold step is valid if either has holdTimeSec > 0 OR requires user confirmation
        return holdTimeSec > 0 || requiresUserConfirmation;
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

    // 支持 target_depth 为字符串（位置引用）或数值
    QJsonValue depthValue = json.value("target_depth");
    if (depthValue.isString()) {
        step.targetDepthRaw = depthValue.toString();
        step.targetDepthMm = 0.0;  // 稍后在 loadSteps 中解析
    } else {
        double depthMm = depthValue.toDouble(0.0);
        step.targetDepthMm = depthMm;
        step.targetDepthRaw = QString::number(depthMm);
    }

    step.presetId = json.value("param_id").toString();
    step.timeoutSec = json.value("timeout").toInt(0);
    step.holdTimeSec = json.value("hold_time").toInt(0);
    step.requiresUserConfirmation = json.value("requires_user_confirmation").toBool(false);

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

    if (requiresUserConfirmation) {
        json["requires_user_confirmation"] = true;
    }

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
    , m_mdbWorker(nullptr)
    , m_motorWorker(nullptr)
    , m_stepTimeoutTimer(new QTimer(this))
    , m_holdTimer(new QTimer(this))
    , m_sensorWatchdogTimer(new QTimer(this))
    , m_lastDepthMm(0.0)
    , m_lastVelocityMmPerMin(0.0)
    , m_lastTorqueNm(0.0)
    , m_lastForceUpperN(0.0)
    , m_lastForceLowerN(0.0)
    , m_lastPressureN(0.0)
    , m_lastStallDetected(false)
    , m_lastSensorDataMs(0)
    , m_hasActivePreset(false)
    , m_totalTargetDepth(0.0)
{
    m_stepTimeoutTimer->setSingleShot(true);
    m_holdTimer->setSingleShot(true);

    connect(m_stepTimeoutTimer, &QTimer::timeout,
            this, &AutoDrillManager::onStepTimeout);
    connect(m_holdTimer, &QTimer::timeout,
            this, &AutoDrillManager::onHoldTimeout);
    connect(m_watchdog, &SafetyWatchdog::faultOccurred,
            this, &AutoDrillManager::onWatchdogFault);

    // 传感器掉线检测定时器 - 每500ms检测一次
    connect(m_sensorWatchdogTimer, &QTimer::timeout,
            this, &AutoDrillManager::onSensorWatchdogTimeout);

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

    // 加载任务特定的位置字典
    if (root.contains("positions") && root.value("positions").isObject()) {
        QJsonObject positionsObj = root.value("positions").toObject();
        m_positions.clear();
        for (auto it = positionsObj.begin(); it != positionsObj.end(); ++it) {
            QString key = it.key();
            double value = it.value().toDouble(-1.0);
            if (value < 0.0) {
                QString error = tr("位置 '%1' 的值无效: %2").arg(key).arg(it.value().toString());
                emit logMessage(error);
                emit taskFailed(error);
                return false;
            }
            m_positions.insert(key, value);
            emit logMessage(tr("加载任务位置: %1 = %2 mm").arg(key).arg(value));
        }
    }

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
    m_positions.clear();  // 清理位置字典
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
    m_lastSensorDataMs = 0;

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

    // Check if current step is a confirmation hold
    const TaskStep& currentStep = m_steps[m_currentStepIndex];
    if (currentStep.type == TaskStep::Type::Hold &&
        currentStep.requiresUserConfirmation) {
        emit logMessage(tr("用户已确认，继续执行"));
        completeCurrentStep();  // Complete and advance to next step
        return true;
    }

    // Original resume logic for motion steps
    if (m_watchdog && m_hasActivePreset) {
        m_watchdog->arm(m_activePreset);
    }

    emit logMessage(tr("恢复执行任务"));
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
    if (!m_mdbWorker || !m_motorWorker) {
        return false;
    }

    const bool connected = m_mdbWorker->isConnected() && m_motorWorker->isConnected();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 staleMs = 2000;
    const bool recentData = (m_lastSensorDataMs > 0) && ((nowMs - m_lastSensorDataMs) <= staleMs);
    return connected || recentData;
}

void AutoDrillManager::onDataBlockReceived(const DataBlock& block)
{
    if (block.values.isEmpty()) {
        return;
    }

    // 提取最新值
    double latestValue = block.values.last();

    m_lastSensorDataMs = QDateTime::currentMSecsSinceEpoch();

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
                                      m_lastTorqueNm, m_lastPressureN,
                                      m_lastForceUpperN, m_lastForceLowerN);
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

void AutoDrillManager::onSensorWatchdogTimeout()
{
    // 仅在运动状态下检测传感器掉线
    if (m_state != AutoTaskState::Moving && m_state != AutoTaskState::Drilling) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 staleMs = 2000;  // 2秒无数据视为掉线

    // 检查传感器数据是否过时
    if (m_lastSensorDataMs > 0 && (nowMs - m_lastSensorDataMs) > staleMs) {
        QString error = tr("⚠️ 传感器数据中断！上次接收: %1ms前")
            .arg(nowMs - m_lastSensorDataMs);
        emit logMessage(error);
        failTask(tr("传感器掉线 - 安全停机"));
        return;
    }

    // 检查 Worker 连接状态
    if (m_mdbWorker && !m_mdbWorker->isConnected()) {
        emit logMessage(tr("⚠️ Modbus传感器连接断开！"));
        failTask(tr("Modbus传感器掉线 - 安全停机"));
        return;
    }

    if (m_motorWorker && !m_motorWorker->isConnected()) {
        emit logMessage(tr("⚠️ 电机传感器连接断开！"));
        failTask(tr("电机传感器掉线 - 安全停机"));
        return;
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
        return false;
    }

    m_stepExecutionState = StepExecutionState::Pending;
    const TaskStep& step = m_steps[m_currentStepIndex];
    emit stepStarted(m_currentStepIndex, step);
    emit logMessage(tr("开始步骤 %1/%2: %3")
                        .arg(m_currentStepIndex + 1)
                        .arg(m_steps.size())
                        .arg(TaskStep::typeToString(step.type)));
    executeStep(step);
    return true;
}

void AutoDrillManager::executeStep(const TaskStep& step)
{
    m_stepExecutionState = StepExecutionState::InProgress;
    m_stepElapsed.restart();

    if (step.type == TaskStep::Type::Hold) {
        if (m_watchdog) {
            m_watchdog->disarm();
            m_watchdog->clearFault();
        }
        m_hasActivePreset = false;
        m_activePreset = DrillParameterPreset();

        // Handle user confirmation mode
        if (step.requiresUserConfirmation) {
            // Stop all controllers for safety during confirmation wait
            stopAllControllers();

            // Stop timeout timer - confirmation holds wait indefinitely
            if (m_stepTimeoutTimer) {
                m_stepTimeoutTimer->stop();
            }

            setState(AutoTaskState::Paused,
                     tr("等待用户确认 - 请点击「继续」按钮"));
            emit logMessage(tr("[暂停] 等待用户确认"));
            m_pauseRequested = true;
            return;  // Do NOT start holdTimer - wait indefinitely
        }

        // Original timer-based hold logic
        int holdSeconds = qMax(1, step.holdTimeSec);
        if (m_holdTimer) {
            m_holdTimer->start(holdSeconds * 1000);
        }

        // Start step timeout timer for normal holds
        if (step.timeoutSec > 0 && m_stepTimeoutTimer) {
            m_stepTimeoutTimer->start(step.timeoutSec * 1000);
        } else if (m_stepTimeoutTimer) {
            m_stepTimeoutTimer->stop();
        }

        setState(AutoTaskState::Moving,
                 tr("保持位置 %1 秒").arg(holdSeconds));
        return;
    }

    // Start step timeout timer for motion steps
    if (step.timeoutSec > 0 && m_stepTimeoutTimer) {
        m_stepTimeoutTimer->start(step.timeoutSec * 1000);
    } else if (m_stepTimeoutTimer) {
        m_stepTimeoutTimer->stop();
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

    // 启动传感器掉线检测定时器（运动时每500ms检测一次）
    if (m_sensorWatchdogTimer && !m_sensorWatchdogTimer->isActive()) {
        m_sensorWatchdogTimer->start(500);
    }

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
    if (m_sensorWatchdogTimer) {
        m_sensorWatchdogTimer->stop();
    }

    if (m_watchdog) {
        m_watchdog->disarm();
        m_watchdog->clearFault();
    }

    m_hasActivePreset = false;
    m_activePreset = DrillParameterPreset();

    emit logMessage(tr("步骤 %1 完成").arg(m_currentStepIndex + 1));
    emit stepCompleted(m_currentStepIndex);
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
    if (m_sensorWatchdogTimer) {
        m_sensorWatchdogTimer->stop();
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
    } else if (sensor == "force_upper") {
        currentValue = m_lastForceUpperN;
    } else if (sensor == "force_lower") {
        currentValue = m_lastForceLowerN;
    } else if (sensor == "feed_velocity") {
        currentValue = std::abs(m_lastVelocityMmPerMin);  // Use absolute value for velocity
    } else if (sensor == "feed_depth") {
        currentValue = m_lastDepthMm;
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

    // Support array format (original)
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
    // Support object format (new, more convenient)
    else if (presetsValue.isObject()) {
        QJsonObject presetsObject = presetsValue.toObject();
        for (auto it = presetsObject.begin(); it != presetsObject.end(); ++it) {
            if (!it.value().isObject()) {
                continue;
            }
            DrillParameterPreset preset = DrillParameterPreset::fromJson(it.value().toObject());
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

    for (int i = 0; i < array.size(); ++i) {
        const QJsonValue& value = array[i];
        if (!value.isObject()) {
            continue;
        }

        TaskStep step = TaskStep::fromJson(value.toObject());

        // 如果 targetDepthRaw 是位置引用或需要解析，立即解析并验证
        if (step.type != TaskStep::Type::Hold) {
            if (step.targetDepthRaw.startsWith("@") ||
                (step.targetDepthMm == 0.0 && !step.targetDepthRaw.isEmpty())) {

                QString errorMsg;
                if (!resolvePosition(step.targetDepthRaw, step.targetDepthMm, errorMsg)) {
                    QString error = tr("步骤 %1 位置解析失败: %2").arg(i + 1).arg(errorMsg);
                    emit logMessage(error);
                    emit taskFailed(error);
                    return false;
                }

                emit logMessage(tr("步骤 %1: %2 解析为 %3 mm")
                    .arg(i + 1).arg(step.targetDepthRaw).arg(step.targetDepthMm));
            }
        }

        if (!step.isValid()) {
            qWarning() << "[AutoDrillManager] Invalid step" << (i + 1) << "ignored";
            continue;
        }

        m_steps.append(step);

        // 更新最大目标深度
        if (step.requiresMotion() && step.targetDepthMm > m_totalTargetDepth) {
            m_totalTargetDepth = step.targetDepthMm;
        }
    }

    if (m_steps.isEmpty()) {
        QString error = tr("任务文件中没有有效的步骤");
        emit logMessage(error);
        emit taskFailed(error);
        return false;
    }

    return true;
}

bool AutoDrillManager::resolvePosition(const QString& positionRef,
                                       double& outDepthMm,
                                       QString& errorMsg)
{
    // 1. 检查是否是位置引用（以 @ 开头）
    if (!positionRef.startsWith("@")) {
        // 尝试解析为数值
        bool ok = false;
        outDepthMm = positionRef.toDouble(&ok);
        if (!ok) {
            errorMsg = tr("无效的深度值: '%1'").arg(positionRef);
            return false;
        }
        return true;
    }

    // 2. 提取位置键（去掉 @ 前缀）
    QString key = positionRef.mid(1).trimmed();
    if (key.isEmpty()) {
        errorMsg = tr("位置引用不能为空: '%1'").arg(positionRef);
        return false;
    }

    // 3. 优先查找任务文件的 positions 字典
    if (m_positions.contains(key)) {
        outDepthMm = m_positions.value(key);
        return true;
    }

    // 4. 查找 mechanisms.json 的 key_positions (通过 FeedController)
    double mmValue = getKeyPositionFromFeed(key);
    if (mmValue >= 0.0) {
        outDepthMm = mmValue;
        return true;
    }

    // 5. 找不到位置键
    errorMsg = tr("未找到位置 '%1'，既不在任务文件的 positions 中，也不在 mechanisms.json 的 key_positions 中").arg(key);
    return false;
}

double AutoDrillManager::getKeyPositionFromFeed(const QString& key) const
{
    if (!m_feed) {
        return -1.0;
    }

    // FeedController::getKeyPositionMm() 返回 mm 值
    return m_feed->getKeyPositionMm(key);
}
