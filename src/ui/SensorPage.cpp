#include "ui/SensorPage.h"
#include "ui_SensorPage.h"
#include "control/AcquisitionManager.h"
#include "dataACQ/VibrationWorker.h"
#include "dataACQ/MdbWorker.h"
#include "dataACQ/MotorWorker.h"
#include <QMessageBox>
#include <QDebug>

SensorPage::SensorPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SensorPage)
    , m_acquisitionManager(nullptr)
    , m_vk701Connected(false)
    , m_mdbConnected(false)
    , m_motorConnected(false)
{
    ui->setupUi(this);
    setupConnections();
    updateUIState();
}

SensorPage::~SensorPage()
{
    delete ui;
}

void SensorPage::setAcquisitionManager(AcquisitionManager *manager)
{
    m_acquisitionManager = manager;
    
    if (m_acquisitionManager) {
        // 连接Manager信号
        connect(m_acquisitionManager, &AcquisitionManager::acquisitionStateChanged,
                this, &SensorPage::onAcquisitionStateChanged);
        connect(m_acquisitionManager, &AcquisitionManager::roundChanged,
                this, &SensorPage::onRoundChanged);
        connect(m_acquisitionManager, &AcquisitionManager::errorOccurred,
                this, &SensorPage::onErrorOccurred);
        connect(m_acquisitionManager, &AcquisitionManager::statisticsUpdated,
                this, &SensorPage::onStatisticsUpdated);
    }
}

void SensorPage::setupConnections()
{
    // VK701控制
    connect(ui->btn_vk701_connect, &QPushButton::clicked, this, &SensorPage::onVK701ConnectClicked);
    connect(ui->btn_vk701_disconnect, &QPushButton::clicked, this, &SensorPage::onVK701DisconnectClicked);
    connect(ui->spin_vk701_frequency, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SensorPage::onVK701FrequencyChanged);
    
    // Modbus控制
    connect(ui->btn_mdb_connect, &QPushButton::clicked, this, &SensorPage::onMdbConnectClicked);
    connect(ui->btn_mdb_disconnect, &QPushButton::clicked, this, &SensorPage::onMdbDisconnectClicked);
    connect(ui->spin_mdb_frequency, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SensorPage::onMdbFrequencyChanged);
    
    // ZMotion控制
    connect(ui->btn_motor_connect, &QPushButton::clicked, this, &SensorPage::onMotorConnectClicked);
    connect(ui->btn_motor_disconnect, &QPushButton::clicked, this, &SensorPage::onMotorDisconnectClicked);
    connect(ui->spin_motor_frequency, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SensorPage::onMotorFrequencyChanged);
    
    // 采集控制
    connect(ui->btn_start_all, &QPushButton::clicked, this, &SensorPage::onStartAll);
    connect(ui->btn_stop_all, &QPushButton::clicked, this, &SensorPage::onStopAll);
    connect(ui->btn_new_round, &QPushButton::clicked, this, &SensorPage::onStartNewRound);
    connect(ui->btn_end_round, &QPushButton::clicked, this, &SensorPage::onEndRound);
}

void SensorPage::onVK701ConnectClicked()
{
    if (!m_acquisitionManager) return;
    
    QString address = ui->le_vk701_address->text();
    int port = ui->spin_vk701_port->value();
    int cardId = ui->spin_vk701_cardid->value();
    
    qDebug() << "[SensorPage] Connecting to VK701:" << address << ":" << port << "Card ID:" << cardId;
    ui->label_status->setText("正在连接 VK701...");
    
    // 配置VibrationWorker
    auto *worker = m_acquisitionManager->vibrationWorker();
    if (!worker) {
        QMessageBox::warning(this, "错误", "VibrationWorker 未初始化");
        ui->label_status->setText("连接失败：Worker未初始化");
        return;
    }
    
    worker->setCardId(cardId);
    worker->setPort(port);
    worker->setServerAddress(address);
    
    // 使用 QMetaObject::invokeMethod 在工作线程中调用测试连接
    bool connected = false;
    QMetaObject::invokeMethod(worker, "testConnection", 
                              Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, connected));
    
    if (connected) {
        m_vk701Connected = true;
        updateUIState();
        ui->label_status->setText("VK701已连接");
        QMessageBox::information(this, "连接成功", 
            QString("VK701已连接\n地址: %1:%2\n卡号: %3").arg(address).arg(port).arg(cardId));
        qDebug() << "[SensorPage] VK701 connected successfully";
    } else {
        m_vk701Connected = false;
        updateUIState();
        ui->label_status->setText("VK701连接失败");
        QMessageBox::critical(this, "连接失败", 
            QString("无法连接到 VK701 服务器\n地址: %1:%2\n\n请检查：\n1. 模拟器是否已启动\n2. IP地址和端口是否正确（端口必须是8234）\n3. 防火墙设置").arg(address).arg(port));
        qDebug() << "[SensorPage] VK701 connection failed";
    }
}

void SensorPage::onVK701DisconnectClicked()
{
    if (!m_acquisitionManager) return;
    
    qDebug() << "[SensorPage] Disconnecting VK701";
    
    auto *worker = m_acquisitionManager->vibrationWorker();
    if (worker && worker->isConnected()) {
        worker->disconnect();
    }
    
    m_vk701Connected = false;
    updateUIState();
    ui->label_status->setText("VK701已断开");
}

void SensorPage::onVK701FrequencyChanged()
{
    int freq = ui->spin_vk701_frequency->value();
    qDebug() << "[SensorPage] VK701频率设置为:" << freq << "Hz";
    
    if (m_acquisitionManager && m_acquisitionManager->vibrationWorker()) {
        m_acquisitionManager->vibrationWorker()->setSampleRate(freq);
    }
}

void SensorPage::onMdbConnectClicked()
{
    if (!m_acquisitionManager) return;
    
    QString address = ui->le_mdb_address->text();
    int port = ui->spin_mdb_port->value();
    
    qDebug() << "[SensorPage] Connecting to Modbus TCP:" << address << ":" << port;
    ui->label_status->setText("正在连接 Modbus TCP...");
    
    // 配置MdbWorker
    auto *worker = m_acquisitionManager->mdbWorker();
    if (!worker) {
        QMessageBox::warning(this, "错误", "MdbWorker 未初始化");
        ui->label_status->setText("连接失败：Worker未初始化");
        return;
    }
    
    worker->setServerAddress(address);
    worker->setServerPort(port);
    
    // 使用 QMetaObject::invokeMethod 在工作线程中调用测试连接
    bool connected = false;
    QMetaObject::invokeMethod(worker, "testConnection", 
                              Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, connected));
    
    if (connected) {
        m_mdbConnected = true;
        updateUIState();
        ui->label_status->setText("Modbus TCP已连接");
        QMessageBox::information(this, "连接成功", 
            QString("Modbus TCP已连接\n地址: %1:%2").arg(address).arg(port));
        qDebug() << "[SensorPage] Modbus TCP connected successfully";
    } else {
        m_mdbConnected = false;
        updateUIState();
        ui->label_status->setText("Modbus TCP连接失败");
        QMessageBox::critical(this, "连接失败", 
            QString("无法连接到 Modbus TCP 服务器\n地址: %1:%2\n\n请检查：\n1. 模拟器是否已启动\n2. IP地址和端口是否正确\n3. 防火墙设置").arg(address).arg(port));
        qDebug() << "[SensorPage] Modbus TCP connection failed";
    }
}

void SensorPage::onMdbDisconnectClicked()
{
    if (!m_acquisitionManager) return;
    
    qDebug() << "[SensorPage] Disconnecting Modbus TCP";
    
    auto *worker = m_acquisitionManager->mdbWorker();
    if (worker && worker->isConnected()) {
        worker->disconnect();
    }
    
    m_mdbConnected = false;
    updateUIState();
    ui->label_status->setText("Modbus TCP已断开");
    qDebug() << "[SensorPage] Modbus TCP disconnected";
}

void SensorPage::onMdbFrequencyChanged()
{
    int freq = ui->spin_mdb_frequency->value();
    qDebug() << "[SensorPage] Modbus频率设置为:" << freq << "Hz";
    
    if (m_acquisitionManager && m_acquisitionManager->mdbWorker()) {
        m_acquisitionManager->mdbWorker()->setSampleRate(freq);
    }
}

void SensorPage::onMotorConnectClicked()
{
    if (!m_acquisitionManager) return;
    
    QString address = ui->le_motor_address->text();
    
    qDebug() << "[SensorPage] Connecting to ZMotion:" << address;
    ui->label_status->setText("正在连接 ZMotion...");
    
    // 配置MotorWorker
    auto *worker = m_acquisitionManager->motorWorker();
    if (!worker) {
        QMessageBox::warning(this, "错误", "MotorWorker 未初始化");
        ui->label_status->setText("连接失败：Worker未初始化");
        return;
    }
    
    worker->setControllerAddress(address);
    
    // 使用 QMetaObject::invokeMethod 在工作线程中调用测试连接
    bool connected = false;
    QMetaObject::invokeMethod(worker, "testConnection", 
                              Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, connected));
    
    if (connected) {
        m_motorConnected = true;
        updateUIState();
        ui->label_status->setText("ZMotion已连接");
        QMessageBox::information(this, "连接成功", 
            QString("ZMotion已连接\n地址: %1").arg(address));
        qDebug() << "[SensorPage] ZMotion connected successfully";
    } else {
        m_motorConnected = false;
        updateUIState();
        ui->label_status->setText("ZMotion连接失败");
        QMessageBox::critical(this, "连接失败", 
            QString("无法连接到 ZMotion 控制器\n地址: %1\n\n请检查：\n1. 模拟器是否已启动\n2. IP地址是否正确（默认：192.168.1.11）\n3. 防火墙设置").arg(address));
        qDebug() << "[SensorPage] ZMotion connection failed";
    }
}

void SensorPage::onMotorDisconnectClicked()
{
    if (!m_acquisitionManager) return;
    
    qDebug() << "[SensorPage] Disconnecting ZMotion";
    
    auto *worker = m_acquisitionManager->motorWorker();
    if (worker && worker->isConnected()) {
        worker->disconnect();
    }
    
    m_motorConnected = false;
    updateUIState();
    ui->label_status->setText("ZMotion已断开");
}

void SensorPage::onMotorFrequencyChanged()
{
    int freq = ui->spin_motor_frequency->value();
    qDebug() << "[SensorPage] Motor频率设置为:" << freq << "Hz";
    
    if (m_acquisitionManager && m_acquisitionManager->motorWorker()) {
        m_acquisitionManager->motorWorker()->setSampleRate(freq);
    }
}

void SensorPage::onStartAll()
{
    if (!m_acquisitionManager) {
        QMessageBox::warning(this, "错误", "AcquisitionManager未初始化");
        return;
    }
    
    qDebug() << "[SensorPage] Starting all acquisition...";
    m_acquisitionManager->startAll();
}

void SensorPage::onStopAll()
{
    if (!m_acquisitionManager) return;
    
    qDebug() << "[SensorPage] Stopping all acquisition...";
    m_acquisitionManager->stopAll();
}

void SensorPage::onStartNewRound()
{
    if (!m_acquisitionManager) return;
    
    QString operatorName = ui->le_operator->text();
    QString note = ui->te_note->toPlainText();
    
    qDebug() << "[SensorPage] Starting new round...";
    m_acquisitionManager->startNewRound(operatorName, note);
}

void SensorPage::onEndRound()
{
    if (!m_acquisitionManager) return;
    
    qDebug() << "[SensorPage] Ending current round...";
    m_acquisitionManager->endCurrentRound();
}

void SensorPage::onAcquisitionStateChanged(bool isRunning)
{
    qDebug() << "[SensorPage] Acquisition state changed:" << isRunning;
    
    if (isRunning) {
        ui->label_status->setText("采集运行中...");
        ui->btn_start_all->setEnabled(false);
        ui->btn_stop_all->setEnabled(true);
    } else {
        ui->label_status->setText("采集已停止");
        ui->btn_start_all->setEnabled(true);
        ui->btn_stop_all->setEnabled(false);
    }
}

void SensorPage::onRoundChanged(int roundId)
{
    qDebug() << "[SensorPage] Round changed to:" << roundId;
    ui->label_round_id->setText(QString("当前轮次: %1").arg(roundId));
}

void SensorPage::onErrorOccurred(const QString &workerName, const QString &error)
{
    qDebug() << "[SensorPage] Error from" << workerName << ":" << error;
    QMessageBox::critical(this, "采集错误", 
        QString("Worker: %1\n错误: %2").arg(workerName, error));
}

void SensorPage::onStatisticsUpdated(const QString &info)
{
    ui->label_statistics->setText(info);
}

void SensorPage::updateUIState()
{
    // VK701按钮状态
    ui->btn_vk701_connect->setEnabled(!m_vk701Connected);
    ui->btn_vk701_disconnect->setEnabled(m_vk701Connected);
    
    // Modbus按钮状态
    ui->btn_mdb_connect->setEnabled(!m_mdbConnected);
    ui->btn_mdb_disconnect->setEnabled(m_mdbConnected);
    
    // Motor按钮状态
    ui->btn_motor_connect->setEnabled(!m_motorConnected);
    ui->btn_motor_disconnect->setEnabled(m_motorConnected);
    
    // 采集控制按钮（至少一个设备连接才能启动）
    bool anyConnected = m_vk701Connected || m_mdbConnected || m_motorConnected;
    ui->btn_start_all->setEnabled(anyConnected && !m_acquisitionManager->isRunning());
}
