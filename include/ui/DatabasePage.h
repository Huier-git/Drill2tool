#ifndef DATABASEPAGE_H
#define DATABASEPAGE_H

#include <QWidget>
#include <QFutureWatcher>
#include <QtConcurrent>
#include "qcustomplot.h"
#include "database/DataQuerier.h"

QT_BEGIN_NAMESPACE
namespace Ui { class DatabasePage; }
QT_END_NAMESPACE

class DatabasePage : public QWidget
{
    Q_OBJECT

public:
    explicit DatabasePage(QWidget *parent = nullptr);
    ~DatabasePage();

private slots:
    void onRefreshRounds();
    void onRoundSelected();
    void onQuery();
    void onExecSql();

    // 快捷选择
    void onSelectAll();
    void onSelectFirst10();
    void onSelectLast10();

    // 时间范围变化
    void onStartSecChanged(int value);
    void onEndSecChanged(int value);

    // 异步查询和导出
    void onQueryFinished();
    void onExportClicked();

    // 图表交互
    void onScalarPlotClicked(QMouseEvent* event);
    void onVibrationPlotClicked(QMouseEvent* event);
    void onTableRowSelected();
    void onTimePreviewClicked(QMouseEvent* event);

private:
    void loadRoundsList();
    void updateRoundInfo(int roundId, qint64 durationSec);
    void displayQueryResult(const QList<DataQuerier::WindowData> &dataList);

    // 图表相关
    void setupPlots();
    void setupTimePreviewPlot();
    void updateScalarPlot(const QList<DataQuerier::WindowData>& data);
    void updateVibrationPlot(const QList<DataQuerier::WindowData>& data);
    void updateTimePreviewPlot();
    void configureChartDarkTheme(QCustomPlot* plot);

    // 图表同步交互
    void linkChartsXAxis();
    void syncTableToChart(double timeInSeconds);
    void syncChartToTable(int row);
    void updateChartCursor(double timeInSeconds);

    // 导出相关
    void startExportAsync(const QString& filePath);

private:
    Ui::DatabasePage *ui;
    DataQuerier *m_querier;

    // 图表控件
    QCustomPlot* m_scalarPlot;
    QCustomPlot* m_vibrationPlot;
    QCustomPlot* m_timePreviewPlot;  // 迷你趋势图（时间选择器）

    // 图表辅助元素
    QCPItemLine* m_cursorLine;  // 游标线（用于同步交互）

    // 异步查询
    QFutureWatcher<QList<DataQuerier::WindowData>> m_queryWatcher;

    // 当前选中轮次信息
    int m_currentRoundId;
    qint64 m_currentRoundStartUs;
    qint64 m_currentRoundDurationSec;
};

#endif // DATABASEPAGE_H
