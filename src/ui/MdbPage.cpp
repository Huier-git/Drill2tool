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
{
    for (int i = 0; i < 4; ++i) m_plots[i] = nullptr;
    ui->setupUi(this);
    setupUI();
    setupConnections();
    initPlot();
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
}

void MdbPage::setupConnections()
{
    connect(ui->btn_start, &QPushButton::clicked, this, &MdbPage::onStartClicked);
    connect(ui->btn_stop, &QPushButton::clicked, this, &MdbPage::onStopClicked);
    connect(ui->btn_zero, &QPushButton::clicked, this, &MdbPage::onZeroClicked);
    connect(ui->btn_clear, &QPushButton::clicked, this, &MdbPage::onClearClicked);
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
        return;
    }

    double value = block.values.isEmpty() ? 0.0 : block.values.first();
    m_latestValues[idx] = value;
    appendHistory(idx, value);
    updateValueDisplay();
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
    if (m_timeAxis.size() >= m_maxPoints) {
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

    refreshPlot();
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

        m_plots[i]->graph(0)->setData(m_timeAxis, vals);

        // 计算Y轴范围
        auto [minIt, maxIt] = std::minmax_element(vals.begin(), vals.end());
        double minY = *minIt;
        double maxY = *maxIt;
        double padding = qMax(1.0, (maxY - minY) * 0.2);

        m_plots[i]->xAxis->setRange(qMax(0, m_sampleIndex - m_maxPoints), m_sampleIndex);
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
