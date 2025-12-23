#include "ui/SensorPage.h"
#include "ui_SensorPage.h"
#include "control/AcquisitionManager.h"
#include "control/zmcaux.h"
#include "Global.h"
#include "dataACQ/VibrationWorker.h"
#include "dataACQ/MdbWorker.h"
#include "dataACQ/MotorWorker.h"
#include <QMessageBox>
#include <QDebug>
#include <QMutexLocker>

SensorPage::SensorPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SensorPage)
    , m_acquisitionManager(nullptr)
    , m_vk701Connected(false)
    , m_mdbConnected(false)
    , m_motorConnected(false)
    , m_resetPending(false)
    , m_lastRoundId(0)
    , m_resetTargetRound(0)
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
    connect(ui->btn_reset_round, &QPushButton::clicked, this, &SensorPage::onResetRound);
    connect(ui->btn_end_round, &QPushButton::clicked, this, &SensorPage::onEndRound);
}

void SensorPage::onVK701ConnectClicked()
{
    if (!m_acquisitionManager) return;

    int cardId = ui->spin_vk701_cardid->value();

    qDebug() << "[SensorPage] Connecting to VK701: Card ID:" << cardId << "(Port 8234 fixed)";
    ui->label_status->setText("正在连接 VK701...");

    auto *worker = m_acquisitionManager->vibrationWorker();
    if (!worker) {
        QMessageBox::warning(this, "错误", "VibrationWorker 未初始化");
        ui->label_status->setText("连接失败：Worker未初始化");
        return;
    }

    worker->setCardId(cardId);

    bool connected = false;
    QMetaObject::invokeMethod(worker, "testConnection",
                              Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, connected));

    if (connected) {
        m_vk701Connected = true;
        updateUIState();
        ui->label_status->setText("VK701已连接");
        QMessageBox::information(this, "连接成功",
            QString("VK701已连接\n卡号: %1\nTCP端口: 8234 (固定)").arg(cardId));
    } else {
        m_vk701Connected = false;
        updateUIState();
        ui->label_status->setText("VK701连接失败");
        QMessageBox::critical(this, "连接失败",
            QString("无法连接到 VK701\n卡号: %1\nTCP端口: 8234").arg(cardId));
    }
}

void SensorPage::onVK701DisconnectClicked()
{
    if (!m_acquisitionManager) return;

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
    // 检查是否正在采集
    if (m_acquisitionManager && m_acquisitionManager->isRunning()) {
        QMessageBox::warning(this, "无法修改频率",
            "VK701硬件采样率在初始化时设定，运行中无法修改。\n\n"
            "请先停止采集，断开连接后重新设置频率再连接。");

        // 恢复到当前Worker的频率值
        if (m_acquisitionManager->vibrationWorker()) {
            int currentFreq = static_cast<int>(m_acquisitionManager->vibrationWorker()->sampleRate());
            ui->spin_vk701_frequency->blockSignals(true);
            ui->spin_vk701_frequency->setValue(currentFreq);
            ui->spin_vk701_frequency->blockSignals(false);
        }
        return;
    }

    int freq = ui->spin_vk701_frequency->value();
    if (m_acquisitionManager && m_acquisitionManager->vibrationWorker()) {
        m_acquisitionManager->vibrationWorker()->setSampleRate(freq);
        qDebug() << "VK701 sample rate changed to:" << freq << "Hz";
    }
}

void SensorPage::onMdbConnectClicked()
{
    if (!m_acquisitionManager) return;

    QString address = ui->le_mdb_address->text();
    int port = ui->spin_mdb_port->value();

    ui->label_status->setText("正在连接 Modbus TCP...");

    auto *worker = m_acquisitionManager->mdbWorker();
    if (!worker) {
        QMessageBox::warning(this, "错误", "MdbWorker 未初始化");
        return;
    }

    worker->setServerAddress(address);
    worker->setServerPort(port);

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
    } else {
        m_mdbConnected = false;
        updateUIState();
        ui->label_status->setText("Modbus TCP连接失败");
        QMessageBox::critical(this, "连接失败",
            QString("无法连接到 Modbus TCP 服务器\n地址: %1:%2").arg(address).arg(port));
    }
}

void SensorPage::onMdbDisconnectClicked()
{
    if (!m_acquisitionManager) return;

    auto *worker = m_acquisitionManager->mdbWorker();
    if (worker && worker->isConnected()) {
        worker->disconnect();
    }

    m_mdbConnected = false;
    updateUIState();
    ui->label_status->setText("Modbus TCP已断开");
}

void SensorPage::onMdbFrequencyChanged()
{
    // 检查是否正在采集
    if (m_acquisitionManager && m_acquisitionManager->isRunning()) {
        QMessageBox::warning(this, "无法修改频率",
            "Modbus TCP 采样频率由定时器控制，运行中修改可能导致数据不一致。\n\n"
            "请先停止采集后再修改频率。");

        // 恢复到当前Worker的频率值
        if (m_acquisitionManager->mdbWorker()) {
            int currentFreq = static_cast<int>(m_acquisitionManager->mdbWorker()->sampleRate());
            ui->spin_mdb_frequency->blockSignals(true);
            ui->spin_mdb_frequency->setValue(currentFreq);
            ui->spin_mdb_frequency->blockSignals(false);
        }
        return;
    }

    int freq = ui->spin_mdb_frequency->value();
    if (m_acquisitionManager && m_acquisitionManager->mdbWorker()) {
        m_acquisitionManager->mdbWorker()->setSampleRate(freq);
        qDebug() << "Modbus TCP sample rate changed to:" << freq << "Hz";
    }
}

void SensorPage::onMotorConnectClicked()
{
    QString address = ui->le_motor_address->text();
    ui->label_status->setText("正在连接 ZMotion...");

    int result = 0;
    bool connected = false;

    {
        QMutexLocker locker(&g_mutex);
        if (g_handle != nullptr) {
            ZAux_Close(g_handle);
            g_handle = nullptr;
        }

        QByteArray ipBytes = address.toLocal8Bit();
        result = ZAux_OpenEth(ipBytes.data(), &g_handle);
        connected = (result == 0 && g_handle != nullptr);
        if (!connected) {
            g_handle = nullptr;
        }
    }

    if (connected) {
        m_motorConnected = true;
        updateUIState();
        ui->label_status->setText("ZMotion已连接");
        QMessageBox::information(this, "连接成功",
            QString("ZMotion已连接\n地址: %1").arg(address));

        if (m_acquisitionManager && m_acquisitionManager->motorWorker()) {
            m_acquisitionManager->motorWorker()->setControllerAddress(address);
        }
    } else {
        g_handle = nullptr;
        m_motorConnected = false;
        updateUIState();
        ui->label_status->setText("ZMotion连接失败");
        QMessageBox::critical(this, "连接失败",
            QString("无法连接到 ZMotion 控制器\n地址: %1\n错误代码: %2").arg(address).arg(result));
    }
}

void SensorPage::onMotorDisconnectClicked()
{
    {
        QMutexLocker locker(&g_mutex);
        if (g_handle != nullptr) {
            ZAux_Close(g_handle);
            g_handle = nullptr;
        }
    }

    m_motorConnected = false;
    updateUIState();
    ui->label_status->setText("ZMotion已断开");
}

void SensorPage::onMotorFrequencyChanged()
{
    // 检查是否正在采集
    if (m_acquisitionManager && m_acquisitionManager->isRunning()) {
        QMessageBox::warning(this, "无法修改频率",
            "电机参数采样频率由定时器控制，运行中修改可能导致数据不一致。\n\n"
            "请先停止采集后再修改频率。");

        // 恢复到当前Worker的频率值
        if (m_acquisitionManager->motorWorker()) {
            int currentFreq = static_cast<int>(m_acquisitionManager->motorWorker()->sampleRate());
            ui->spin_motor_frequency->blockSignals(true);
            ui->spin_motor_frequency->setValue(currentFreq);
            ui->spin_motor_frequency->blockSignals(false);
        }
        return;
    }

    int freq = ui->spin_motor_frequency->value();
    if (m_acquisitionManager && m_acquisitionManager->motorWorker()) {
        m_acquisitionManager->motorWorker()->setSampleRate(freq);
        qDebug() << "Motor parameter sample rate changed to:" << freq << "Hz";
    }
}

void SensorPage::onStartAll()
{
    if (!m_acquisitionManager) {
        QMessageBox::warning(this, "错误", "AcquisitionManager未初始化");
        return;
    }
    m_acquisitionManager->startAll();
}

void SensorPage::onStopAll()
{
    if (!m_acquisitionManager) return;
    m_acquisitionManager->stopAll();
}

void SensorPage::onStartNewRound()
{
    if (!m_acquisitionManager) return;

    QString operatorName = ui->le_operator->text();
    QString note = ui->te_note->toPlainText();

    ui->label_status->setText("正在创建新轮次...");
    m_acquisitionManager->startNewRound(operatorName, note);
    // 状态由roundChanged信号更新
}

void SensorPage::onEndRound()
{
    if (!m_acquisitionManager) return;

    ui->label_status->setText("正在结束轮次...");
    m_acquisitionManager->endCurrentRound();
    // 状态由roundChanged信号更新
}

void SensorPage::onResetRound()
{
    if (!m_acquisitionManager) return;

    if (m_resetPending) {
        return;
    }

    int targetRound = ui->spin_reset_target->value();

    if (m_acquisitionManager->isRunning()) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::warning(this, "确认重置轮次",
                                     QString("采集正在运行，重置将停止采集并删除轮次 %1 及之后的所有数据。\n\n"
                                            "此操作不可撤销。是否继续？").arg(targetRound),
                                     QMessageBox::Yes | QMessageBox::No,
                                     QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }

        m_resetPending = true;
        m_resetTargetRound = targetRound;
        disconnect(m_resetConnection);
        m_resetConnection = connect(
            m_acquisitionManager, &AcquisitionManager::acquisitionStateChanged,
            this, [this, targetRound](bool running) {
                if (running) {
                    return;
                }
                disconnect(m_resetConnection);
                m_resetConnection = QMetaObject::Connection();
                m_resetPending = false;
                if (!m_acquisitionManager) {
                    return;
                }
                ui->label_status->setText("正在重置轮次...");
                m_acquisitionManager->resetCurrentRound(targetRound);
                // 状态由roundChanged信号更新
                QMessageBox::information(this, "重置完成",
                                        QString("轮次数据已清除，下次新建轮次将从 %1 开始。").arg(targetRound));
                m_resetTargetRound = 0;
            });

        ui->label_status->setText("正在停止采集...");
        m_acquisitionManager->stopAll();
        return;
    }

    // 确认对话框
    QMessageBox::StandardButton reply;
    reply = QMessageBox::warning(this, "确认重置轮次",
                                 QString("警告：将删除轮次 %1 及之后的所有数据！\n\n"
                                        "下次新建轮次将从轮次 %1 开始。\n\n"
                                        "此操作不可撤销。是否继续？").arg(targetRound),
                                 QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        m_resetTargetRound = targetRound;
        ui->label_status->setText("正在重置轮次...");
        m_acquisitionManager->resetCurrentRound(targetRound);
        // 状态由roundChanged信号更新
        QMessageBox::information(this, "重置完成",
                                QString("轮次数据已清除，下次新建轮次将从 %1 开始。").arg(targetRound));
        m_resetTargetRound = 0;
    }
}

void SensorPage::onAcquisitionStateChanged(bool isRunning)
{
    if (isRunning) {
        ui->label_status->setText("采集运行中...");
    } else {
        ui->label_status->setText("采集已停止");
    }
    updateUIState();
}

void SensorPage::onRoundChanged(int roundId)
{
    if (roundId == 0) {
        ui->label_round_id->setText("系统状态: 空闲 (未开始轮次)");
        // 判断是结束轮次还是重置轮次
        if (m_lastRoundId > 0) {
            if (m_resetTargetRound > 0) {
                ui->label_status->setText(QString("已重置到轮次 %1").arg(m_resetTargetRound));
            } else {
                ui->label_status->setText("轮次已结束");
            }
        }
    } else {
        ui->label_round_id->setText(QString("当前轮次: %1 (进行中)").arg(roundId));
        // 判断是新建轮次
        if (roundId > m_lastRoundId) {
            ui->label_status->setText(QString("新建轮次 %1 成功").arg(roundId));
        }
    }
    m_lastRoundId = roundId;
}

void SensorPage::onErrorOccurred(const QString &workerName, const QString &error)
{
    QMessageBox::critical(this, "采集错误",
        QString("Worker: %1\n错误: %2").arg(workerName, error));
}

void SensorPage::onStatisticsUpdated(const QString &info)
{
    ui->label_statistics->setText(info);
}

void SensorPage::updateUIState()
{
    bool isRunning = m_acquisitionManager && m_acquisitionManager->isRunning();

    ui->btn_vk701_connect->setEnabled(!m_vk701Connected && !isRunning);
    ui->btn_vk701_disconnect->setEnabled(m_vk701Connected && !isRunning);
    ui->spin_vk701_frequency->setEnabled(!isRunning);

    ui->btn_mdb_connect->setEnabled(!m_mdbConnected && !isRunning);
    ui->btn_mdb_disconnect->setEnabled(m_mdbConnected && !isRunning);
    ui->spin_mdb_frequency->setEnabled(!isRunning);

    ui->btn_motor_connect->setEnabled(!m_motorConnected && !isRunning);
    ui->btn_motor_disconnect->setEnabled(m_motorConnected && !isRunning);
    ui->spin_motor_frequency->setEnabled(!isRunning);

    bool anyConnected = m_vk701Connected || m_mdbConnected || m_motorConnected;
    ui->btn_start_all->setEnabled(anyConnected && !isRunning);
    ui->btn_stop_all->setEnabled(isRunning);
}
