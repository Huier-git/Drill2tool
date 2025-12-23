#ifndef MOTORPAGE_H
#define MOTORPAGE_H

#include <QWidget>
#include "dataACQ/DataTypes.h"
#include "control/UnitConverter.h"

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
    void checkConnectionStatus();
    void onUnitToggled(bool checked);

private:
    void setupUI();
    void setupConnections();
    void updateValueDisplay(int motorId, SensorType type, double value);
    void updateUnitLabels();  // 更新单位标签
    double convertValue(double driverValue, int motorId, UnitValueType type) const;
    AxisUnitInfo getAxisUnitInfo(int motorId) const;

private:
    Ui::MotorPage *ui;
    AcquisitionManager *m_acquisitionManager;
    MotorWorker *m_worker;

    bool m_isRunning;
    bool m_displayPhysicalUnits;  // false=脉冲, true=物理单位
    QTimer *m_connectionCheckTimer;
};

#endif // MOTORPAGE_H