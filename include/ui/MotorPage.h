#ifndef MOTORPAGE_H
#define MOTORPAGE_H

#include <QWidget>
#include "dataACQ/DataTypes.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MotorPage; }
QT_END_NAMESPACE

class AcquisitionManager;
class MotorWorker;
struct DataBlock;

class MotorPage : public QWidget
{
    Q_OBJECT

public:
    explicit MotorPage(QWidget *parent = nullptr);
    ~MotorPage();

    void setAcquisitionManager(AcquisitionManager *manager);

private slots:
    void onStartClicked();
    void onStopClicked();
    void onDataBlockReceived(const DataBlock &block);
    void onWorkerStateChanged(WorkerState state);
    void onStatisticsUpdated(qint64 samplesCollected, double sampleRate);

private:
    void setupUI();
    void setupConnections();
    void updateValueDisplay(int motorId, SensorType type, double value);

private:
    Ui::MotorPage *ui;
    AcquisitionManager *m_acquisitionManager;
    MotorWorker *m_worker;

    bool m_isRunning;
};

#endif // MOTORPAGE_H