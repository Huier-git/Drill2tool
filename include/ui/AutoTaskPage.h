#ifndef AUTOTASKPAGE_H
#define AUTOTASKPAGE_H

#include <QWidget>
#include <QString>
#include <QMap>

#include "control/DrillParameterPreset.h"
#include "control/AutoDrillManager.h"

namespace Ui {
class AutoTaskPage;
}

class FeedController;
class RotationController;
class PercussionController;
class AcquisitionManager;
class QTimer;

class AutoTaskPage : public QWidget
{
    Q_OBJECT

public:
    explicit AutoTaskPage(QWidget *parent = nullptr);
    ~AutoTaskPage();

    void setControllers(FeedController* feed,
                        RotationController* rotation,
                        PercussionController* percussion);

    void setAcquisitionManager(AcquisitionManager* manager);

private slots:
    // UI button slots
    void onLoadTaskClicked();
    void onReloadClicked();
    void onImportTaskClicked();
    void onStartClicked();
    void onPauseClicked();
    void onResumeClicked();
    void onStopClicked();
    void onEmergencyClicked();

    // Task list slots
    void onTaskListItemClicked();
    void onTaskListItemDoubleClicked();

    // AutoDrillManager signal handlers
    void onTaskStateChanged(AutoTaskState state, const QString& message);
    void onStepStarted(int index, const TaskStep& step);
    void onStepCompleted(int index);
    void onProgressUpdated(double depthMm, double percent);
    void onTaskCompleted();
    void onTaskFailed(const QString& reason);
    void onLogMessage(const QString& message);

    // Timer slots
    void onElapsedTimerTick();

    // Test mode slots (always declared to avoid moc issues)
    void onRunUnitTestsClicked();
    void onTestScenarioNormalClicked();
    void onTestScenarioTorqueClicked();
    void onTestScenarioPressureClicked();
    void onTestScenarioStallClicked();
    void onTestScenarioProgressiveClicked();
    void onStopMockDataClicked();

private:
    void setupConnections();
    void loadTasksFromDirectory();
    void updateTaskList();
    void updateStepsTable();
    void updatePresetsTable();
    void updateUIState();
    void updateStepStatus(int stepIndex, const QString& status);
    void highlightCurrentStep(int stepIndex);
    void appendLog(const QString& message);

    QString formatStepType(TaskStep::Type type) const;
    QString formatStepTarget(const TaskStep& step) const;
    QString formatElapsedTime(qint64 msec) const;

    Ui::AutoTaskPage *ui;

    FeedController* m_feedController;
    RotationController* m_rotationController;
    PercussionController* m_percussionController;

    AutoDrillManager* m_drillManager;
    QTimer* m_elapsedTimer;
    QElapsedTimer m_taskElapsed;

    QString m_tasksDirectory;
    QStringList m_availableTasks;
    QString m_currentTaskFile;

#ifdef ENABLE_TEST_MODE
    class MockDataGenerator* m_mockGenerator;
    void setupTestUI();
#endif
};

#endif // AUTOTASKPAGE_H
