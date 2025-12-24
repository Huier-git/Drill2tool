#include "ui/VibrationPage.h"
#include "ui_VibrationPage.h"
#include "control/AcquisitionManager.h"
#include "dataACQ/VibrationWorker.h"
#include "dataACQ/DataTypes.h"
#include <QDebug>
#include <QMessageBox>
#include <algorithm>

// 图表颜色定义（参考原vk701page）
const QColor PLOT_COLORS[3] = {Qt::darkRed, Qt::darkGreen, Qt::darkBlue};

VibrationPage::VibrationPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::VibrationPage)
    , m_acquisitionManager(nullptr)
    , m_vibrationWorker(nullptr)
    , m_displayPoints(1000)
    , m_isAcquiring(false)
    , m_totalSamples(0)
    , m_currentSampleRate(5000.0)
{
    ui->setupUi(this);
    setupUI();
    setupConnections();
    initializePlots();
}

VibrationPage::~VibrationPage()
{
    delete ui;
}

void VibrationPage::setAcquisitionManager(AcquisitionManager *manager)
{
    m_acquisitionManager = manager;

    // 从管理器获取VibrationWorker指针
    if (m_acquisitionManager) {
        m_vibrationWorker = m_acquisitionManager->vibrationWorker();

        if (m_vibrationWorker) {
            // 连接Worker信号到页面槽函数
            connect(m_vibrationWorker, &BaseWorker::dataBlockReady,
                    this, &VibrationPage::onDataBlockReceived, Qt::QueuedConnection);
            connect(m_vibrationWorker, &BaseWorker::stateChanged,
                    this, &VibrationPage::onWorkerStateChanged, Qt::QueuedConnection);
            connect(m_vibrationWorker, &BaseWorker::statisticsUpdated,
                    this, &VibrationPage::onStatisticsUpdated, Qt::QueuedConnection);

            qDebug() << "[VibrationPage] Connected to VibrationWorker";
        }

        qDebug() << "[VibrationPage] AcquisitionManager set";
    }
}

void VibrationPage::setupUI()
{
    // 获取UI中的QCustomPlot控件指针
    m_plots[0] = ui->plot_ch1;
    m_plots[1] = ui->plot_ch2;
    m_plots[2] = ui->plot_ch3;

    // 设置按钮样式类型
    ui->btn_start->setProperty("type", "success");  // 开始采集 - 绿色
    ui->btn_stop->setProperty("type", "warning");  // 停止采集 - 橙色
    ui->btn_pause->setProperty("type", "warning");  // 暂停显示 - 橙色

    // 刷新按钮样式
    ui->btn_start->style()->unpolish(ui->btn_start);
    ui->btn_start->style()->polish(ui->btn_start);
    ui->btn_stop->style()->unpolish(ui->btn_stop);
    ui->btn_stop->style()->polish(ui->btn_stop);
    ui->btn_pause->style()->unpolish(ui->btn_pause);
    ui->btn_pause->style()->polish(ui->btn_pause);

    // 设置初始状态
    ui->btn_pause->setEnabled(false);
    ui->label_status->setText("状态: 就绪");
    ui->label_statistics->setText("采样数: 0 | 采样率: 0 Hz");
}

void VibrationPage::setupConnections()
{
    // 连接控制按钮
    connect(ui->btn_start, &QPushButton::clicked, this, &VibrationPage::onStartClicked);
    connect(ui->btn_stop, &QPushButton::clicked, this, &VibrationPage::onStopClicked);
    connect(ui->btn_pause, &QPushButton::clicked, this, &VibrationPage::onPauseClicked);
}

void VibrationPage::initializePlots()
{
    for (int i = 0; i < 3; i++) {
        QCustomPlot *plot = m_plots[i];

        // 添加一条图形线
        plot->addGraph();
        plot->graph(0)->setPen(QPen(PLOT_COLORS[i], 1.5));

        // 设置坐标轴标签
        plot->xAxis->setLabel("时间 (ms)");
        plot->yAxis->setLabel("振动 (mV)");

        // 设置坐标轴范围
        plot->xAxis->setRange(0, m_displayPoints);
        plot->yAxis->setRange(-2000, 2000);  // 初始范围，根据实际数据调整

        // 绘制完整坐标轴框
        plot->axisRect()->setupFullAxesBox();

        // 初始化空数据
        QVector<double> x(m_displayPoints), y(m_displayPoints);
        for (int j = 0; j < m_displayPoints; j++) {
            x[j] = j;
            y[j] = 0;
        }
        plot->graph(0)->setData(x, y);
        plot->replot();
    }

    qDebug() << "[VibrationPage] Plots initialized with" << m_displayPoints << "points each";
}

void VibrationPage::onStartClicked()
{
    qDebug() << "[VibrationPage] Start button clicked";

    if (!m_acquisitionManager) {
        QMessageBox::warning(this, "错误", "采集管理器未初始化");
        return;
    }

    // 关键：检查VK701是否已连接
    if (!m_vibrationWorker) {
        QMessageBox::warning(this, "错误", "VibrationWorker未初始化");
        return;
    }

    if (!m_vibrationWorker->isConnected()) {
        QMessageBox::critical(this, "连接错误",
            "VK701采集卡未连接！\n\n"
            "请先在【数据采集】页面：\n"
            "1. 配置VK701连接参数\n"
            "2. 点击【连接】按钮\n"
            "3. 确认连接成功后再启动采集");
        qDebug() << "[VibrationPage] Cannot start: VK701 not connected";
        return;
    }

    qDebug() << "[VibrationPage] VK701 is connected, starting acquisition...";

    // 启动振动采集（采样频率已在SensorPage设置）
    m_acquisitionManager->startVibration();

    // 更新UI状态
    ui->btn_start->setEnabled(false);
    ui->btn_pause->setEnabled(true);
    ui->label_status->setText("状态: 采集中...");

    m_isAcquiring = true;
    m_totalSamples = 0;

    qDebug() << "[VibrationPage] Acquisition start command sent";
}

void VibrationPage::onStopClicked()
{
    qDebug() << "[VibrationPage] Stop button clicked";

    if (!m_acquisitionManager || !m_vibrationWorker) {
        return;
    }

    // 停止采集
    m_acquisitionManager->stopVibration();

    // 清空图表
    clearAllPlots();

    // 重置状态
    ui->btn_start->setEnabled(true);
    ui->btn_pause->setEnabled(false);
    ui->btn_pause->setText("暂停");
    ui->label_status->setText("状态: 已停止");
    m_isAcquiring = false;
    m_totalSamples = 0;
    ui->label_statistics->setText("采样数: 0 | 采样率: 0 Hz");

    qDebug() << "[VibrationPage] Stop command sent, plots cleared";
}

void VibrationPage::onPauseClicked()
{
    qDebug() << "[VibrationPage] Pause button clicked";

    if (!m_acquisitionManager || !m_vibrationWorker) {
        return;
    }

    // 根据当前状态切换暂停/恢复
    if (m_isAcquiring) {
        // 暂停采集
        QMetaObject::invokeMethod(m_vibrationWorker, "pause", Qt::QueuedConnection);

        ui->btn_start->setEnabled(true);
        ui->btn_pause->setText("恢复");
        ui->label_status->setText("状态: 已暂停");
        m_isAcquiring = false;

        qDebug() << "[VibrationPage] Pause command sent";
    } else {
        // 恢复采集
        QMetaObject::invokeMethod(m_vibrationWorker, "resume", Qt::QueuedConnection);

        ui->btn_start->setEnabled(false);
        ui->btn_pause->setText("暂停");
        ui->label_status->setText("状态: 采集中...");
        m_isAcquiring = true;

        qDebug() << "[VibrationPage] Resume command sent";
    }
}

void VibrationPage::onDataBlockReceived(const DataBlock &block)
{
    // 检查是否是振动数据
    if (block.sensorType != SensorType::Vibration_X &&
        block.sensorType != SensorType::Vibration_Y &&
        block.sensorType != SensorType::Vibration_Z) {
        return;  // 不是振动数据，忽略
    }

    // 确定通道ID（0, 1, 2 对应 X, Y, Z）
    int channelId = block.channelId;
    if (channelId < 0 || channelId >= 3) {
        qWarning() << "[VibrationPage] Invalid channel ID:" << channelId;
        return;
    }

    // 解析BLOB数据
    int numSamples = block.numSamples;
    const float *floatData = reinterpret_cast<const float*>(block.blobData.constData());

    // 准备时间和数值数据
    QVector<double> timeData(numSamples);
    QVector<double> valueData(numSamples);

    for (int i = 0; i < numSamples; i++) {
        timeData[i] = i;  // 简化时间轴（0, 1, 2, ...）
        valueData[i] = floatData[i] * 1000.0;  // 转换为mV（参考原实现）
    }

    // 更新图表
    updatePlot(channelId, timeData, valueData);

    // 更新统计信息（每100个数据块更新一次）
    static int blockCounter = 0;
    blockCounter++;
    if (blockCounter % 100 == 0) {
        m_totalSamples += numSamples;
        onStatisticsUpdated(m_totalSamples, block.sampleRate);
    }
}

void VibrationPage::updatePlot(int channelId, const QVector<double> &timeData, const QVector<double> &valueData)
{
    if (channelId < 0 || channelId >= 3) {
        return;
    }

    QCustomPlot *plot = m_plots[channelId];

    // 更新图表数据（参考原vk701page的方式：清除并重新添加）
    plot->graph(0)->setData(timeData, valueData);

    // 自动调整X轴范围
    plot->xAxis->setRange(0, timeData.size());

    // 重绘
    plot->replot();
}

void VibrationPage::clearAllPlots()
{
    for (int i = 0; i < 3; i++) {
        QVector<double> x(m_displayPoints), y(m_displayPoints);
        for (int j = 0; j < m_displayPoints; j++) {
            x[j] = j;
            y[j] = 0;
        }
        m_plots[i]->graph(0)->setData(x, y);
        m_plots[i]->replot();
    }
}

void VibrationPage::onWorkerStateChanged(WorkerState state)
{
    // 根据WorkerState更新UI
    QString statusText;
    switch (state) {
        case WorkerState::Stopped:
            statusText = "状态: 已停止";
            ui->btn_start->setEnabled(true);
            ui->btn_pause->setEnabled(false);
            ui->btn_pause->setText("暂停");
            m_isAcquiring = false;
            break;
        case WorkerState::Running:
            statusText = "状态: 采集中...";
            ui->btn_start->setEnabled(false);
            ui->btn_pause->setEnabled(true);
            ui->btn_pause->setText("暂停");
            m_isAcquiring = true;
            break;
        case WorkerState::Paused:
            statusText = "状态: 已暂停";
            ui->btn_start->setEnabled(true);
            ui->btn_pause->setEnabled(true);
            ui->btn_pause->setText("恢复");
            m_isAcquiring = false;
            break;
        case WorkerState::Starting:
            statusText = "状态: 启动中...";
            ui->btn_start->setEnabled(false);
            ui->btn_pause->setEnabled(false);
            break;
        case WorkerState::Stopping:
            statusText = "状态: 停止中...";
            ui->btn_start->setEnabled(false);
            ui->btn_pause->setEnabled(false);
            break;
        case WorkerState::Pausing:
            statusText = "状态: 暂停中...";
            ui->btn_start->setEnabled(false);
            ui->btn_pause->setEnabled(false);
            break;
        case WorkerState::Error:
            statusText = "状态: 错误";
            ui->btn_start->setEnabled(true);
            ui->btn_pause->setEnabled(false);
            ui->btn_pause->setText("暂停");
            m_isAcquiring = false;
            break;
        default:
            statusText = "状态: 未知";
            break;
    }
    ui->label_status->setText(statusText);
    qDebug() << "[VibrationPage] Worker state changed:" << statusText;
}

void VibrationPage::onStatisticsUpdated(qint64 samplesCollected, double sampleRate)
{
    m_totalSamples = samplesCollected;
    m_currentSampleRate = sampleRate;

    QString statsText = QString("采样数: %1 | 采样率: %2 Hz")
                            .arg(samplesCollected)
                            .arg(sampleRate, 0, 'f', 0);
    ui->label_statistics->setText(statsText);
}


