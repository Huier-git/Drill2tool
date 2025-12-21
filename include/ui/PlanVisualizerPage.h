#ifndef PLANVISUALIZERPAGE_H
#define PLANVISUALIZERPAGE_H

#include <QWidget>
#include <QProcess>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>

class QCustomPlot;
class QCPBars;

namespace Ui {
class PlanVisualizerPage;
}

// 甘特图任务数据
struct GanttTask {
    QString name;
    QString dof;
    int start;
    int end;
    QString opType;  // move, spin, hold
};

/**
 * @brief 钻杆规划可视化页面
 *
 * 功能：
 * 1. 运行Python脚本计算串行/优化规划
 * 2. 显示ASCII甘特图
 * 3. 使用QCustomPlot绘制甘特图
 * 4. 导出JSON格式供控制系统使用
 * 5. 可编辑的时长配置表格
 * 6. 重新规划功能
 */
class PlanVisualizerPage : public QWidget
{
    Q_OBJECT

public:
    explicit PlanVisualizerPage(QWidget *parent = nullptr);
    ~PlanVisualizerPage();

private slots:
    void onRunPlan();
    void onReplan();
    void onEditDurations();
    void onExportJson();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void onProcessReadyRead();

    // 时长配置操作
    void onLoadDurConfig();
    void onSaveDurConfig();
    void onResetDurConfig();
    void onDurTableCellChanged(int row, int column);

private:
    void setupGanttChart();
    void setupConnections();
    void setupDurTable();
    void runPythonScript(const QString& scriptName, int nPipes, int zoom);
    void parseAsciiOutput(const QString& output);
    void updateStatistics(int serialTime, int optimizedTime);
    void updateGanttChart(const QList<GanttTask>& tasks, const QList<int>& stageCuts);
    QString getPythonPath() const;
    QString getScriptPath(const QString& scriptName) const;
    QString getDurConfigPath() const;

    // 时长配置
    void loadDefaultDurations();
    void populateDurTable();
    bool saveDurationsToJson(const QString& filePath, bool showErrors = true);
    bool loadDurationsFromJson(const QString& filePath, bool showErrors = true);
    void writeDurationsToTempFile();

    // 解析ASCII提取任务
    QList<GanttTask> parseTasksFromAscii(const QString& ascii);
    QList<int> parseStageCutsFromAscii(const QString& ascii);

    // JSON导出
    QJsonObject convertToJson(const QString& asciiOutput);

private:
    Ui::PlanVisualizerPage *ui;
    QCustomPlot *m_ganttChart;
    QProcess *m_process;

    QString m_currentAscii;
    QJsonObject m_currentJson;
    int m_serialTime;
    int m_optimizedTime;
    bool m_isOptimizedMode;

    // 时长配置数据
    QMap<QString, int> m_durations;
    bool m_durationsModified;
};

#endif // PLANVISUALIZERPAGE_H
