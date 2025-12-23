#ifndef MDBPAGE_H
#define MDBPAGE_H

#include <QWidget>
#include <QVector>
#include <QMap>
#include <QTimer>
#include "dataACQ/DataTypes.h"
#include "qcustomplot.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MdbPage; }
QT_END_NAMESPACE

class AcquisitionManager;
class MdbWorker;
struct DataBlock;

/**
 * @brief Modbus传感器实时监测页面
 *
 * 功能：
 * - 启停 MDB 采集（调用 AcquisitionManager）
 * - 显示 4 路传感器最新值（上拉力/下拉力/扭矩/位移）
 * - 简单实时曲线（最近 N 个点）
 * - 零点校准、清屏
 */
class MdbPage : public QWidget
{
    Q_OBJECT

public:
    explicit MdbPage(QWidget *parent = nullptr);
    ~MdbPage();

    void setAcquisitionManager(AcquisitionManager *manager);

private slots:
    void onStartClicked();
    void onStopClicked();
    void onZeroClicked();
    void onClearClicked();
    void onDisplayModeChanged(int index);
    void onDisplayPointsChanged(int value);
    void onPlotRefreshTimeout();

    void onDataBlockReceived(const DataBlock &block);
    void onWorkerStateChanged(WorkerState state);
    void onStatisticsUpdated(qint64 samplesCollected, double sampleRate);

private:
    void setupUI();
    void setupConnections();
    void initPlot();
    void updateValueDisplay();
    void appendHistory(int channelIndex, double value);
    void refreshPlot();
    int sensorTypeToIndex(SensorType type) const;

private:
    Ui::MdbPage *ui;
    AcquisitionManager *m_acquisitionManager;
    MdbWorker *m_worker;

    QCustomPlot *m_plots[4];                 // 4 separate plots
    QVector<QVector<double>> m_valueHistory; // [channel][points]
    QVector<double> m_timeAxis;              // shared time index
    QVector<double> m_latestValues;          // latest per channel
    int m_maxPoints;
    int m_sampleIndex;
    double m_currentSampleRate;
    bool m_isRunning;

    // 显示模式控制
    QTimer *m_plotRefreshTimer;              // 图表刷新定时器
    bool m_slidingWindowMode;                // true=滑动窗口, false=全部显示
    bool m_plotNeedsUpdate;                  // 标记是否有新数据需要刷新
};

#endif // MDBPAGE_H
