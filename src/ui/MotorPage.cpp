#include "ui/MotorPage.h"
#include "ui_MotorPage.h"
#include "control/AcquisitionManager.h"
#include "dataACQ/MotorWorker.h"
#include <QDebug>
#include <QMessageBox>

MotorPage::MotorPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::MotorPage)
    , m_acquisitionManager(nullptr)
    , m_worker(nullptr)
    , m_isRunning(false)
{
    ui->setupUi(this);
    setupUI();
    setupConnections();
}

MotorPage::~MotorPage()
{
    delete ui;
}

void MotorPage::setAcquisitionManager(AcquisitionManager *manager)
{
    m_acquisitionManager = manager;
    if (!m_acquisitionManager) return;

    m_worker = m_acquisitionManager->motorWorker();
    if (m_worker) {
        connect(m_worker, &BaseWorker::dataBlockReady,
                this, &MotorPage::onDataBlockReceived, Qt::QueuedConnection);
        connect(m_worker, &BaseWorker::stateChanged,
                this, &MotorPage::onWorkerStateChanged, Qt::QueuedConnection);
        connect(m_worker, &BaseWorker::statisticsUpdated,
                this, &MotorPage::onStatisticsUpdated, Qt::QueuedConnection);
    }
}

void MotorPage::setupUI()
{
    ui->btn_start->setEnabled(true);
    ui->btn_stop->setEnabled(false);
}

void MotorPage::setupConnections()
{
    connect(ui->btn_start, &QPushButton::clicked, this, &MotorPage::onStartClicked);
    connect(ui->btn_stop, &QPushButton::clicked, this, &MotorPage::onStopClicked);
}

void MotorPage::onStartClicked()
{
    if (!m_acquisitionManager || !m_worker) {
        QMessageBox::warning(this, "错误", "MotorWorker 未初始化");
        return;
    }

    if (!m_worker->isConnected()) {
        QMessageBox::critical(this, "连接错误",
            "ZMotion 运动控制器未连接！\n\n"
            "请先在【数据采集配置】页面：\n"
            "1. 配置 ZMotion IP地址\n"
            "2. 点击【连接】按钮\n"
            "3. 确认连接成功后再启动采集");
        return;
    }

    qDebug() << "[MotorPage] Starting Motor worker";
    m_acquisitionManager->startMotor();
}

void MotorPage::onStopClicked()
{
    if (m_acquisitionManager) {
        m_acquisitionManager->stopMotor();
    }
}

void MotorPage::onDataBlockReceived(const DataBlock &block)
{
    double val = block.values.isEmpty() ? 0.0 : block.values.first();
    updateValueDisplay(block.channelId, block.sensorType, val);
}

void MotorPage::updateValueDisplay(int motorId, SensorType type, double value)
{
    QLCDNumber *target = nullptr;
    
    // 映射 motorId (0-7) 和传感器类型到 UI 控件
    // 注意：UI 中的 Motor #1 对应 motorId 0
    switch (motorId) {
    case 0: // Motor 1
        if (type == SensorType::Motor_Position) target = ui->lcd_m1_pos;
        else if (type == SensorType::Motor_Speed) target = ui->lcd_m1_speed;
        else if (type == SensorType::Motor_Torque) target = ui->lcd_m1_torque;
        else if (type == SensorType::Motor_Current) target = ui->lcd_m1_current;
        break;
    case 1: // Motor 2
        if (type == SensorType::Motor_Position) target = ui->lcd_m2_pos;
        else if (type == SensorType::Motor_Speed) target = ui->lcd_m2_speed;
        else if (type == SensorType::Motor_Torque) target = ui->lcd_m2_torque;
        else if (type == SensorType::Motor_Current) target = ui->lcd_m2_current;
        break;
    case 2: // Motor 3
        if (type == SensorType::Motor_Position) target = ui->lcd_m3_pos;
        else if (type == SensorType::Motor_Speed) target = ui->lcd_m3_speed;
        else if (type == SensorType::Motor_Torque) target = ui->lcd_m3_torque;
        else if (type == SensorType::Motor_Current) target = ui->lcd_m3_current;
        break;
    case 3: // Motor 4
        if (type == SensorType::Motor_Position) target = ui->lcd_m4_pos;
        else if (type == SensorType::Motor_Speed) target = ui->lcd_m4_speed;
        else if (type == SensorType::Motor_Torque) target = ui->lcd_m4_torque;
        else if (type == SensorType::Motor_Current) target = ui->lcd_m4_current;
        break;
    case 4: // Motor 5
        if (type == SensorType::Motor_Position) target = ui->lcd_m5_pos;
        else if (type == SensorType::Motor_Speed) target = ui->lcd_m5_speed;
        else if (type == SensorType::Motor_Torque) target = ui->lcd_m5_torque;
        else if (type == SensorType::Motor_Current) target = ui->lcd_m5_current;
        break;
    case 5: // Motor 6
        if (type == SensorType::Motor_Position) target = ui->lcd_m6_pos;
        else if (type == SensorType::Motor_Speed) target = ui->lcd_m6_speed;
        else if (type == SensorType::Motor_Torque) target = ui->lcd_m6_torque;
        else if (type == SensorType::Motor_Current) target = ui->lcd_m6_current;
        break;
    case 6: // Motor 7
        if (type == SensorType::Motor_Position) target = ui->lcd_m7_pos;
        else if (type == SensorType::Motor_Speed) target = ui->lcd_m7_speed;
        else if (type == SensorType::Motor_Torque) target = ui->lcd_m7_torque;
        else if (type == SensorType::Motor_Current) target = ui->lcd_m7_current;
        break;
    case 7: // Motor 8
        if (type == SensorType::Motor_Position) target = ui->lcd_m8_pos;
        else if (type == SensorType::Motor_Speed) target = ui->lcd_m8_speed;
        else if (type == SensorType::Motor_Torque) target = ui->lcd_m8_torque;
        else if (type == SensorType::Motor_Current) target = ui->lcd_m8_current;
        break;
    }

    if (target) {
        target->display(value);
    }
}

void MotorPage::onWorkerStateChanged(WorkerState state)
{
    m_isRunning = (state == WorkerState::Running);
    ui->btn_start->setEnabled(!m_isRunning);
    ui->btn_stop->setEnabled(m_isRunning);
    
    QString status;
    switch (state) {
        case WorkerState::Running: status = "状态：采集中"; break;
        case WorkerState::Stopped: status = "状态：已停止"; break;
        case WorkerState::Error: status = "状态：错误"; break;
        default: status = "状态：..."; break;
    }
    ui->label_status->setText(status);
}

void MotorPage::onStatisticsUpdated(qint64 samplesCollected, double sampleRate)
{
    Q_UNUSED(samplesCollected);
    Q_UNUSED(sampleRate);
}