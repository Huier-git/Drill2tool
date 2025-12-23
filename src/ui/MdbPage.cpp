#include "ui/MdbPage.h"
#include "ui_MdbPage.h"
#include "control/AcquisitionManager.h"
#include "dataACQ/MdbWorker.h"
#include <QMessageBox>
#include <QMetaObject>
#include <QDebug>
#include <algorithm>

namespace {
const QColor MDB_COLORS[4] = {Qt::darkRed, Qt::darkGreen, Qt::darkBlue, Qt::darkMagenta};
}

MdbPage::MdbPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::MdbPage)
    , m_acquisitionManager(nullptr)
    , m_worker(nullptr)
    , m_maxPoints(300)
    , m_sampleIndex(0)
    , m_currentSampleRate(10.0)
    , m_isRunning(false)
    , m_plotRefreshTimer(nullptr)
    , m_slidingWindowMode(true)
    , m_plotNeedsUpdate(false)
{
    for (int i = 0; i < 4; ++i) m_plots[i] = nullptr;
    ui->setupUi(this);
    setupUI();
    setupConnections();
    initPlot();

    // 图表刷新定时器：20Hz足够流畅，减少CPU占用
    m_plotRefreshTimer = new QTimer(this);
    m_plotRefreshTimer->setInterval(50);
    connect(m_plotRefreshTimer, &QTimer::timeout, this, &MdbPage::onPlotRefreshTimeout);
    m_plotRefreshTimer->start();
}

MdbPage::~MdbPage()
{
    delete ui;
}

void MdbPage::setAcquisitionManager(AcquisitionManager *manager)
{
    m_acquisitionManager = manager;
    if (!m_acquisitionManager) {
        return;
    }

    m_worker = m_acquisitionManager->mdbWorker();
    if (m_worker) {
        connect(m_worker, &BaseWorker::dataBlockReady,
                this, &MdbPage::onDataBlockReceived, Qt::QueuedConnection);
        connect(m_worker, &BaseWorker::stateChanged,
                this, &MdbPage::onWorkerStateChanged, Qt::QueuedConnection);
        connect(m_worker, &BaseWorker::statisticsUpdated,
                this, &MdbPage::onStatisticsUpdated, Qt::QueuedConnection);
    }
}

void MdbPage::setupUI()
{
    m_latestValues = QVector<double>(4, 0.0);
    m_valueHistory = QVector<QVector<double>>(4);
    for (auto &vec : m_valueHistory) {
        vec.reserve(m_maxPoints);
    }

    // 设置按钮样式类型
    ui->btn_start->setProperty("type", "success");  // 开始采集 - 绿色
    ui->btn_stop->setProperty("type", "warning");  // 停止采集 - 橙色

    // 刷新按钮样式
    ui->btn_start->style()->unpolish(ui->btn_start);
    ui->btn_start->style()->polish(ui->btn_start);
    ui->btn_stop->style()->unpolish(ui->btn_stop);
    ui->btn_stop->style()->polish(ui->btn_stop);
}

void MdbPage::setupConnections()
{
    connect(ui->btn_start, &QPushButton::clicked, this, &MdbPage::onStartClicked);
    connect(ui->btn_stop, &QPushButton::clicked, this, &MdbPage::onStopClicked);
    connect(ui->btn_zero, &QPushButton::clicked, this, &MdbPage::onZeroClicked);
    connect(ui->btn_clear, &QPushButton::clicked, this, &MdbPage::onClearClicked);

    // 显示模式控件连接
    connect(ui->combo_displayMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MdbPage::onDisplayModeChanged);
    connect(ui->spin_displayPoints, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MdbPage::onDisplayPointsChanged);
}

void MdbPage::initPlot()
{
    m_plots[0] = ui->plot_force_upper;
    m_plots[1] = ui->plot_force_lower;
    m_plots[2] = ui->plot_torque;
    m_plots[3] = ui->plot_position;

    QStringList titles = {"上拉力 (N)", "下拉力 (N)", "扭矩 (N·m)", "位移 (mm)"};

    for (int i = 0; i < 4; ++i) {
        if (!m_plots[i]) continue;

        m_plots[i]->addGraph();
        m_plots[i]->graph(0)->setPen(QPen(MDB_COLORS[i], 1.5));
        m_plots[i]->graph(0)->setName(titles[i]);

        m_plots[i]->xAxis->setLabel("样本点");
        m_plots[i]->yAxis->setLabel(titles[i]);
        m_plots[i]->xAxis->setRange(0, m_maxPoints);
        m_plots[i]->yAxis->setRange(-100, 100);

        // 简化图例
        m_plots[i]->legend->setVisible(false);

        m_plots[i]->replot();
    }
}

void MdbPage::onStartClicked()
{
    if (!m_acquisitionManager || !m_worker) {
        QMessageBox::warning(this, "错误", "MdbWorker 未初始化");
        return;
    }

    if (!m_worker->isConnected()) {
        QMessageBox::critical(this, "连接错误", "Modbus 未连接，请先在“数据采集”页连接后再启动采集。");
        return;
    }

    qDebug() << "[MdbPage] Starting MDB worker";
    m_acquisitionManager->startMdb();
}

void MdbPage::onStopClicked()
{
    if (!m_acquisitionManager || !m_worker) {
        return;
    }

    qDebug() << "[MdbPage] Stopping MDB worker";
    m_acquisitionManager->stopMdb();
}

void MdbPage::onZeroClicked()
{
    if (!m_worker || !m_worker->isConnected()) {
        QMessageBox::warning(this, "提示", "未连接 Modbus，无法零点校准。");
        return;
    }

    qDebug() << "[MdbPage] Performing zero calibration";
    QMetaObject::invokeMethod(m_worker, "performZeroCalibration", Qt::QueuedConnection);
}

void MdbPage::onClearClicked()
{
    for (auto &vec : m_valueHistory) {
        vec.clear();
    }
    m_timeAxis.clear();
    m_sampleIndex = 0;
    refreshPlot();
    qDebug() << "[MdbPage] Cleared history";
}

void MdbPage::onDataBlockReceived(const DataBlock &block)
{
    int idx = sensorTypeToIndex(block.sensorType);
    if (idx < 0 || idx >= m_latestValues.size()) {
        qDebug() << "[MdbPage] Invalid sensor type:" << static_cast<int>(block.sensorType);
        return;
    }

    double value = block.values.isEmpty() ? 0.0 : block.values.first();
    m_latestValues[idx] = value;
    appendHistory(idx, value);
    updateValueDisplay();

    // 调试日志（仅前3个样本）
    static int debugCount = 0;
    if (debugCount < 3) {
        qDebug() << "[MdbPage] Data received - Type:" << static_cast<int>(block.sensorType)
                 << "Index:" << idx << "Value:" << value;
        debugCount++;
    }
}

void MdbPage::onWorkerStateChanged(WorkerState state)
{
    m_isRunning = (state == WorkerState::Running);
    QString status;
    switch (state) {
        case WorkerState::Running: status = "状态：采集中"; break;
        case WorkerState::Paused: status = "状态：已暂停"; break;
        case WorkerState::Starting: status = "状态：启动中"; break;
        case WorkerState::Stopping: status = "状态：停止中"; break;
        case WorkerState::Stopped: status = "状态：已停止"; break;
        default: status = "状态：未知"; break;
    }
    ui->label_status->setText(status);
    ui->btn_start->setEnabled(!m_isRunning);
    ui->btn_stop->setEnabled(m_isRunning);
}

void MdbPage::onStatisticsUpdated(qint64 samplesCollected, double sampleRate)
{
    m_currentSampleRate = sampleRate;
    ui->label_stats->setText(
        QString("采样频率: %1 Hz | 样本数: %2")
            .arg(sampleRate, 0, 'f', 1)
            .arg(samplesCollected));
}

void MdbPage::updateValueDisplay()
{
    ui->lcd_force_upper->display(m_latestValues.value(0));
    ui->lcd_force_lower->display(m_latestValues.value(1));
    ui->lcd_torque->display(m_latestValues.value(2));
    ui->lcd_position->display(m_latestValues.value(3));
}

void MdbPage::appendHistory(int channelIndex, double value)
{
    // 滑动窗口模式：移除旧数据
    if (m_slidingWindowMode && m_timeAxis.size() >= m_maxPoints) {
        if (!m_timeAxis.isEmpty()) m_timeAxis.removeFirst();
        for (auto &vec : m_valueHistory) {
            if (!vec.isEmpty()) vec.removeFirst();
        }
    }

    m_timeAxis.push_back(m_sampleIndex++);
    m_valueHistory[channelIndex].push_back(value);

    // Ensure other channels align with time axis length for plotting
    for (int i = 0; i < m_valueHistory.size(); ++i) {
        if (m_valueHistory[i].size() < m_timeAxis.size()) {
            m_valueHistory[i].push_back(m_valueHistory[i].isEmpty() ? 0.0 : m_valueHistory[i].last());
        }
    }

    // 标记需要刷新，由定时器统一处理
    m_plotNeedsUpdate = true;
}

void MdbPage::refreshPlot()
{
    if (m_timeAxis.isEmpty()) {
        for (int i = 0; i < 4; ++i) {
            if (m_plots[i]) m_plots[i]->replot();
        }
        return;
    }

    for (int i = 0; i < 4; ++i) {
        if (!m_plots[i]) continue;

        const auto &vals = m_valueHistory[i];
        if (vals.isEmpty()) {
            m_plots[i]->replot();
            continue;
        }

        // 根据显示模式决定显示范围
        QVector<double> displayTime;
        QVector<double> displayVals;

        if (m_slidingWindowMode) {
            // 滑动窗口：显示最近m_maxPoints个点
            int startIdx = qMax(0, m_timeAxis.size() - m_maxPoints);
            displayTime = m_timeAxis.mid(startIdx);
            displayVals = vals.mid(startIdx);
        } else {
            // 全部显示
            displayTime = m_timeAxis;
            displayVals = vals;
        }

        m_plots[i]->graph(0)->setData(displayTime, displayVals);

        // 计算Y轴范围
        auto [minIt, maxIt] = std::minmax_element(displayVals.begin(), displayVals.end());
        double minY = *minIt;
        double maxY = *maxIt;
        double padding = qMax(1.0, (maxY - minY) * 0.2);

        // X轴范围
        if (!displayTime.isEmpty()) {
            m_plots[i]->xAxis->setRange(displayTime.first(), displayTime.last());
        }
        m_plots[i]->yAxis->setRange(minY - padding, maxY + padding);
        m_plots[i]->replot();
    }
}

int MdbPage::sensorTypeToIndex(SensorType type) const
{
    switch (type) {
        case SensorType::Force_Upper: return 0;
        case SensorType::Force_Lower: return 1;
        case SensorType::Torque_MDB: return 2;
        case SensorType::Position_MDB: return 3;
        default: return -1;
    }
}

void MdbPage::onDisplayModeChanged(int index)
{
    m_slidingWindowMode = (index == 0);
    ui->spin_displayPoints->setEnabled(m_slidingWindowMode);
    m_plotNeedsUpdate = true;
    qDebug() << "[MdbPage] Display mode:" << (m_slidingWindowMode ? "滑动窗口" : "全部显示");
}

void MdbPage::onDisplayPointsChanged(int value)
{
    m_maxPoints = value;
    m_plotNeedsUpdate = true;
    qDebug() << "[MdbPage] Display points:" << value;
}

void MdbPage::onPlotRefreshTimeout()
{
    if (m_plotNeedsUpdate) {
        m_plotNeedsUpdate = false;
        refreshPlot();
    }
}
