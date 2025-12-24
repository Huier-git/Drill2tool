#ifndef AUTODRILLMANAGER_H
#define AUTODRILLMANAGER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QMetaType>

#include "control/DrillParameterPreset.h"
#include "control/MechanismTypes.h"
#include "dataACQ/DataTypes.h"

class QTimer;
class FeedController;
class RotationController;
class PercussionController;
class SafetyWatchdog;
class MdbWorker;
class MotorWorker;

enum class AutoTaskState {
    Idle,
    Preparing,
    Moving,
    Drilling,
    Paused,
    Finished,
    Error
};

struct TaskStep
{
    enum class Type {
        Positioning,
        Drilling,
        Hold
    };

    Type type = Type::Positioning;
    double targetDepthMm = 0.0;
    QString presetId;
    int timeoutSec = 0;
    int holdTimeSec = 0;
    QJsonObject conditions;
    bool requiresUserConfirmation = false;
    QString targetDepthRaw;  // 原始位置字符串（"@H" 或 "1001.0"），用于错误提示和日志

    bool isValid() const;
    bool requiresMotion() const { return type != Type::Hold; }
    bool isDrillingStep() const { return type == Type::Drilling; }

    static QString typeToString(Type type);
    static Type typeFromString(const QString& typeStr);
    static TaskStep fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
};

class AutoDrillManager : public QObject
{
    Q_OBJECT

public:
    AutoDrillManager(FeedController* feed,
                     RotationController* rotation,
                     PercussionController* percussion,
                     QObject* parent = nullptr);
    ~AutoDrillManager() override;

    bool loadTaskFile(const QString& filePath);
    void clearTask();

    bool start();
    bool pause();
    bool resume();
    void abort();
    void emergencyStop();

    AutoTaskState state() const { return m_state; }
    QString stateString() const;
    const QVector<TaskStep>& steps() const { return m_steps; }
    int currentStepIndex() const { return m_currentStepIndex; }
    const QMap<QString, DrillParameterPreset>& presets() const { return m_presets; }
    SafetyWatchdog* watchdog() const { return m_watchdog; }
    QString taskFilePath() const { return m_taskFilePath; }

    // 数据采集连接
    void setDataWorkers(MdbWorker* mdbWorker, MotorWorker* motorWorker);
    bool hasSensorData() const;

public slots:
    void onDataBlockReceived(const DataBlock& block);

signals:
    void stateChanged(AutoTaskState newState, const QString& message);
    void stepStarted(int index, const TaskStep& step);
    void stepCompleted(int index);
    void progressUpdated(double depthMm, double percent);
    void taskCompleted();
    void taskFailed(const QString& reason);
    void logMessage(const QString& message);

private slots:
    void onFeedTargetReached();
    void onFeedStateChanged(MechanismState state, const QString& msg);
    void onWatchdogFault(const QString& code, const QString& detail);
    void onStepTimeout();
    void onHoldTimeout();
    void onSensorWatchdogTimeout();  // 传感器掉线检测

private:
    enum class StepExecutionState {
        Pending,
        InProgress,
        Completed
    };

    void setState(AutoTaskState newState, const QString& message);
    bool acquireMotionLock(const QString& reason);
    void releaseMotionLock();
    bool prepareNextStep();
    void executeStep(const TaskStep& step);
    void completeCurrentStep();
    void failTask(const QString& reason);
    void stopAllControllers();
    void applyPreset(const DrillParameterPreset& preset, TaskStep::Type type);

    bool evaluateConditions(const TaskStep& step) const;
    bool evaluateSingleCondition(const QJsonObject& condition) const;
    double computeProgressPercent(double depthMm) const;

    bool loadPresets(const QJsonObject& root);
    bool loadSteps(const QJsonArray& array);
    bool resolvePosition(const QString& positionRef, double& outDepthMm, QString& errorMsg);
    double getKeyPositionFromFeed(const QString& key) const;

    AutoTaskState m_state;
    QVector<TaskStep> m_steps;
    QMap<QString, DrillParameterPreset> m_presets;
    QMap<QString, double> m_positions;  // 任务特定的位置字典 (位置名 -> mm值)
    int m_currentStepIndex;
    StepExecutionState m_stepExecutionState;
    QString m_stateMessage;
    QString m_taskFilePath;
    bool m_motionLockAcquired;
    bool m_pauseRequested;
    bool m_abortRequested;

    FeedController* m_feed;
    RotationController* m_rotation;
    PercussionController* m_percussion;
    SafetyWatchdog* m_watchdog;

    // 数据采集Worker
    MdbWorker* m_mdbWorker;
    MotorWorker* m_motorWorker;

    QTimer* m_stepTimeoutTimer;
    QTimer* m_holdTimer;
    QTimer* m_sensorWatchdogTimer;  // 传感器掉线检测定时器
    QElapsedTimer m_stepElapsed;
    double m_lastDepthMm;
    double m_lastVelocityMmPerMin;
    double m_lastTorqueNm;
    double m_lastForceUpperN;      // Upper force sensor (N)
    double m_lastForceLowerN;      // Lower force sensor (N)
    double m_lastPressureN;        // Calculated drilling pressure (N)
    bool m_lastStallDetected;
    qint64 m_lastSensorDataMs;

    DrillParameterPreset m_activePreset;
    bool m_hasActivePreset;

    double m_totalTargetDepth;
};

Q_DECLARE_METATYPE(AutoTaskState)
Q_DECLARE_METATYPE(TaskStep)

#endif // AUTODRILLMANAGER_H
