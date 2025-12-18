#ifndef VIBRATIONPAGE_H
#define VIBRATIONPAGE_H

#include <QWidget>
#include <QMap>
#include <QVector>
#include "qcustomplot.h"
#include "dataACQ/DataTypes.h"

QT_BEGIN_NAMESPACE
namespace Ui { class VibrationPage; }
QT_END_NAMESPACE

class AcquisitionManager;
class VibrationWorker;
struct DataBlock;

/**
 * @brief 振动数据实时监测页面
 *
 * 功能：
 * 1. 显示3通道振动波形（X, Y, Z轴）
 * 2. 控制采集启动/停止/暂停
 * 3. 实时刷新波形显示
 * 4. 显示采集状态和统计信息
 *
 * 注意：
 * - 不包含数据库查询功能（后续统一实现）
 * - 通过AcquisitionManager管理VibrationWorker
 */
class VibrationPage : public QWidget
{
    Q_OBJECT

public:
    explicit VibrationPage(QWidget *parent = nullptr);
    ~VibrationPage();

    /**
     * @brief 设置采集管理器
     * @param manager 采集管理器指针
     */
    void setAcquisitionManager(AcquisitionManager *manager);

private slots:
    // UI控制按钮槽函数
    void onStartClicked();
    void onPauseClicked();

    // 数据处理槽函数
    void onDataBlockReceived(const DataBlock &block);

    // 状态更新槽函数
    void onWorkerStateChanged(WorkerState state);
    void onStatisticsUpdated(qint64 samplesCollected, double sampleRate);

private:
    void setupUI();
    void setupConnections();
    void initializePlots();
    void updatePlot(int channelId, const QVector<double> &timeData, const QVector<double> &valueData);
    void clearAllPlots();

private:
    Ui::VibrationPage *ui;
    AcquisitionManager *m_acquisitionManager;
    VibrationWorker *m_vibrationWorker;  // 从AcquisitionManager获取

    // 图表控件（3通道）
    QCustomPlot *m_plots[3];

    // 数据缓存用于显示（每个通道保留最新的一批数据用于绘图）
    QMap<int, QVector<double>> m_channelTimeData;   // 通道ID -> 时间数据
    QMap<int, QVector<double>> m_channelValueData;  // 通道ID -> 数值数据

    // 显示参数
    int m_displayPoints;         // 每个图表显示的点数
    bool m_isAcquiring;          // 是否正在采集
    qint64 m_totalSamples;       // 总采样数
    double m_currentSampleRate;  // 当前采样率
};

#endif // VIBRATIONPAGE_H
