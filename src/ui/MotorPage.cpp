#include "ui/MotorPage.h"
#include "qstyle.h"
#include "ui_MotorPage.h"
#include "control/AcquisitionManager.h"
#include "control/MotionConfigManager.h"
#include "dataACQ/MotorWorker.h"
#include "Global.h"
#include <QDebug>
#include <QLabel>
#include <QMessageBox>
#include <QTimer>
#include <QMutexLocker>

MotorPage::MotorPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::MotorPage)
    , m_acquisitionManager(nullptr)
    , m_worker(nullptr)
    , m_isRunning(false)
    , m_displayPhysicalUnits(false)  // 默认显示脉冲
    , m_connectionCheckTimer(nullptr)
{
    ui->setupUi(this);
    setupUI();
    setupConnections();

    // 创建连接状态检查定时器
    m_connectionCheckTimer = new QTimer(this);
    m_connectionCheckTimer->setInterval(1000);  // 每秒检查一次
    connect(m_connectionCheckTimer, &QTimer::timeout, this, &MotorPage::checkConnectionStatus);
    m_connectionCheckTimer->start();
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

    // 设置按钮样式类型
    ui->btn_start->setProperty("type", "success");  // 开始采集 - 绿色
    ui->btn_stop->setProperty("type", "warning");  // 停止采集 - 橙色

    // 刷新按钮样式
    ui->btn_start->style()->unpolish(ui->btn_start);
    ui->btn_start->style()->polish(ui->btn_start);
    ui->btn_stop->style()->unpolish(ui->btn_stop);
    ui->btn_stop->style()->polish(ui->btn_stop);
}

void MotorPage::setupConnections()
{
    connect(ui->btn_start, &QPushButton::clicked, this, &MotorPage::onStartClicked);
    connect(ui->btn_stop, &QPushButton::clicked, this, &MotorPage::onStopClicked);
    connect(ui->cb_displayUnits, &QCheckBox::toggled, this, &MotorPage::onUnitToggled);
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
    // 根据传感器类型确定单位换算类型
    UnitValueType unitType;
    if (type == SensorType::Motor_Position) {
        unitType = UnitValueType::Position;
    } else if (type == SensorType::Motor_Speed) {
        unitType = UnitValueType::Speed;
    } else if (type == SensorType::Motor_Torque || type == SensorType::Motor_Current) {
        // 扭矩和电流暂不换算，直接显示原始值
        unitType = UnitValueType::Position;  // 占位，实际不使用
    } else {
        unitType = UnitValueType::Position;
    }

    // 进行单位换算
    double displayValue = value;
    if (type == SensorType::Motor_Position || type == SensorType::Motor_Speed) {
        displayValue = convertValue(value, motorId, unitType);
    }

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
        target->display(displayValue);
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

void MotorPage::checkConnectionStatus()
{
    // 如果正在采集，不覆盖采集状态
    if (m_isRunning) {
        return;
    }

    // 检查 ZMotion 连接状态
    bool connected = false;
    {
        QMutexLocker locker(&g_mutex);
        connected = (g_handle != nullptr);
    }

    if (connected) {
        ui->label_status->setText("系统状态: 已连接");
    } else {
        ui->label_status->setText("系统状态: 未连接");
    }
}

void MotorPage::onUnitToggled(bool checked)
{
    m_displayPhysicalUnits = checked;
    qDebug() << "[MotorPage] Unit display:" << (checked ? "物理单位" : "脉冲");

    // 更新所有电机的单位标签
    updateUnitLabels();
}

void MotorPage::updateUnitLabels()
{
    // 为每个电机更新单位标签
    for (int motorId = 0; motorId < 8; ++motorId) {
        AxisUnitInfo info = getAxisUnitInfo(motorId);

        // 构造单位字符串
        QString posUnit = m_displayPhysicalUnits ? info.unitLabel : "脉冲";
        QString speedUnit;
        if (m_displayPhysicalUnits) {
            speedUnit = (info.unitLabel == "deg") ? "deg/s" : "mm/s";
        } else {
            speedUnit = "脉冲/s";
        }

        // 更新对应的标签（根据motorId找到对应的UI控件）
        QLabel *posLabel = nullptr;
        QLabel *speedLabel = nullptr;

        switch (motorId) {
        case 0:
            posLabel = ui->l_m1_p;
            speedLabel = ui->l_m1_s;
            break;
        case 1:
            posLabel = ui->l_m2_p;
            speedLabel = ui->l_m2_s;
            break;
        case 2:
            posLabel = ui->l_m3_p;
            speedLabel = ui->l_m3_s;
            break;
        case 3:
            posLabel = ui->l_m4_p;
            speedLabel = ui->l_m4_s;
            break;
        case 4:
            posLabel = ui->l_m5_p;
            speedLabel = ui->l_m5_s;
            break;
        case 5:
            posLabel = ui->l_m6_p;
            speedLabel = ui->l_m6_s;
            break;
        case 6:
            posLabel = ui->l_m7_p;
            speedLabel = ui->l_m7_s;
            break;
        case 7:
            posLabel = ui->l_m8_p;
            speedLabel = ui->l_m8_s;
            break;
        }

        if (posLabel) {
            posLabel->setText(QString("位置 (%1)").arg(posUnit));
        }
        if (speedLabel) {
            speedLabel->setText(QString("速度 (%1)").arg(speedUnit));
        }
    }
}

double MotorPage::convertValue(double driverValue, int motorId, UnitValueType type) const
{
    if (!m_displayPhysicalUnits) {
        return driverValue;  // 显示脉冲，不换算
    }

    // 换算为物理单位
    AxisUnitInfo info = getAxisUnitInfo(motorId);
    return UnitConverter::driverToPhysical(driverValue, info, type);
}

AxisUnitInfo MotorPage::getAxisUnitInfo(int motorId) const
{
    // 从配置获取机构名称（通过MotorMap映射）
    QString mechanismCode;
    bool isRotary = false;  // 是否为旋转机构

    // MotorMap: {Pr=0, Pi=1, Fz=2, Cb=3, Mg=4, Mr=5, Me=6, Sr=7}
    switch (motorId) {
        case 0: mechanismCode = "Pr"; isRotary = true; break;   // 旋转
        case 1: mechanismCode = "Pi"; isRotary = false; break;  // 冲击
        case 2: mechanismCode = "Fz"; isRotary = false; break;  // 进给
        case 3: mechanismCode = "Cb"; isRotary = false; break;  // 夹紧（力矩控制）
        case 4: mechanismCode = "Mg"; isRotary = false; break;  // 机械臂抓取（力矩控制）
        case 5: mechanismCode = "Mr"; isRotary = true; break;   // 机械臂旋转
        case 6: mechanismCode = "Me"; isRotary = false; break;  // 机械臂伸缩
        case 7: mechanismCode = "Sr"; isRotary = true; break;   // 仓储旋转
        default: mechanismCode = "Fz"; isRotary = false; break; // 默认
    }

    // 从配置文件获取单位信息
    auto config = MotionConfigManager::instance();
    MechanismParams params = config->getMechanismConfig(mechanismCode);

    AxisUnitInfo info;
    info.code = mechanismCode;
    info.motorIndex = motorId;

    // 根据机构类型选择单位
    if (isRotary) {
        info.unitLabel = "deg";
        info.pulsesPerUnit = params.hasPulsesPerDegree ? params.pulsesPerDegree : 1.0;
    } else {
        info.unitLabel = "mm";
        info.pulsesPerUnit = params.hasPulsesPerMm ? params.pulsesPerMm : 1.0;
    }

    return info;
}
