#include "ui/DrillControlPage.h"
#include "ui_DrillControlPage.h"

#include "control/ZMotionDriver.h"
#include "control/MotionConfigManager.h"
#include "control/FeedController.h"
#include "control/RotationController.h"
#include "control/PercussionController.h"
#include "control/ClampController.h"
#include "control/StorageController.h"
#include "control/DockingController.h"
#include "control/ArmExtensionController.h"
#include "control/ArmGripController.h"
#include "control/ArmRotationController.h"

#include <QDebug>
#include <QDateTime>
#include <QFile>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>

// ============================================================================
// 关键位置元数据定义
// ============================================================================

struct KeyPositionInfo {
    QString key;         // 位置代号 (A, B, C, ...)
    QString description; // 位置说明
};

// 各机构关键位置定义
static const QMap<Mechanism::Code, QVector<KeyPositionInfo>> s_keyPositionMeta = {
    { Mechanism::Fz, {
        {"A", "最底端"},
        {"B", "钻管底端对接结束"},
        {"C", "钻管底端对接开始"},
        {"D", "钻管顶端对接结束"},
        {"E", "钻具顶端对接结束"},
        {"F", "钻管顶端对接开始"},
        {"G", "钻具顶端对接开始"},
        {"H", "最顶端"},
        {"I", "搭载钻管后底部对接结束"},
        {"J", "搭载钻管后顶部对接开始"}
    }},
    { Mechanism::Sr, {
        {"A", "位置0"},
        {"B", "位置1"},
        {"C", "位置2"},
        {"D", "位置3"},
        {"E", "位置4"},
        {"F", "位置5"},
        {"G", "位置6"}
    }},
    { Mechanism::Me, {
        {"A", "完全收回"},
        {"B", "面对存储机构"},
        {"C", "面对对接头"}
    }},
    { Mechanism::Mg, {
        {"A", "完全张开"},
        {"B", "完全夹紧"}
    }},
    { Mechanism::Mr, {
        {"A", "对准存储机构"},
        {"B", "对准对接头"}
    }},
    { Mechanism::Dh, {
        {"A", "完全推出"},
        {"B", "完全收回"}
    }},
    { Mechanism::Pr, {
        {"A", "不旋转"},
        {"B", "正向对接速度"},
        {"C", "逆向对接速度"},
        {"D", "程序调控速度"}
    }},
    { Mechanism::Pi, {
        {"A", "不冲击"},
        {"B", "程序调控冲击"}
    }},
    { Mechanism::Cb, {
        {"A", "完全张开"},
        {"B", "完全夹紧"}
    }}
};

DrillControlPage::DrillControlPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DrillControlPage)
    , m_driver(nullptr)
    , m_ownsDriver(false)
    , m_feedController(nullptr)
    , m_rotationController(nullptr)
    , m_percussionController(nullptr)
    , m_clampController(nullptr)
    , m_storageController(nullptr)
    , m_dockingController(nullptr)
    , m_armExtController(nullptr)
    , m_armGripController(nullptr)
    , m_armRotController(nullptr)
    , m_isConnected(false)
    , m_currentConfigMechanism(Mechanism::Fz)
{
    ui->setupUi(this);

    // 提前加载配置，确保UI能显示正确的默认值
    loadConfig();

    setupUI();
    setupConnections();
    setupConfigTab();

    // 状态更新定时器
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &DrillControlPage::updateStatus);

    appendLog("DrillControlPage initialized");
}

DrillControlPage::~DrillControlPage()
{
    m_statusTimer->stop();
    destroyControllers();

    if (m_ownsDriver && m_driver) {
        delete m_driver;
        m_driver = nullptr;
    }

    delete ui;
}

void DrillControlPage::setDriver(IMotionDriver* driver)
{
    if (m_ownsDriver && m_driver) {
        delete m_driver;
    }

    m_driver = driver;
    m_ownsDriver = false;

    if (m_driver) {
        createControllers();
        m_isConnected = true;
        setStatusLabel(ui->lbl_connection_status, "● 已连接", "#67c23a");
        m_statusTimer->start(200);
        appendLog("External driver connected");
    }
}

void DrillControlPage::setupUI()
{
    // 初始化所有状态标签为"未就绪"
    setStatusLabel(ui->lbl_Fz_status, "未就绪", "#909399");
    setStatusLabel(ui->lbl_Pr_status, "未就绪", "#909399");
    setStatusLabel(ui->lbl_Pi_status, "锁定", "#909399");
    setStatusLabel(ui->lbl_Cb_status, "未就绪", "#909399");
    setStatusLabel(ui->lbl_Sr_status, "未就绪", "#909399");
    setStatusLabel(ui->lbl_Dh_status, "离线", "#909399");
    setStatusLabel(ui->lbl_Me_status, "未就绪", "#909399");
    setStatusLabel(ui->lbl_Mg_status, "未就绪", "#909399");
    setStatusLabel(ui->lbl_Mr_status, "未就绪", "#909399");
}

void DrillControlPage::setupConnections()
{
    // 系统控制
    connect(ui->btn_connect, &QPushButton::clicked, this, &DrillControlPage::onConnectClicked);
    connect(ui->btn_init_all, &QPushButton::clicked, this, &DrillControlPage::onInitAllClicked);
    connect(ui->btn_stop_all, &QPushButton::clicked, this, &DrillControlPage::onStopAllClicked);
    connect(ui->btn_reload_config, &QPushButton::clicked, this, &DrillControlPage::onReloadConfigClicked);

    // Fz - 进给
    connect(ui->btn_Fz_init, &QPushButton::clicked, this, &DrillControlPage::onFzInitClicked);
    connect(ui->btn_Fz_move, &QPushButton::clicked, this, &DrillControlPage::onFzMoveClicked);
    connect(ui->btn_Fz_safe, &QPushButton::clicked, this, &DrillControlPage::onFzSafeClicked);
    connect(ui->btn_Fz_stop, &QPushButton::clicked, this, &DrillControlPage::onFzStopClicked);

    // Pr - 回转
    connect(ui->btn_Pr_init, &QPushButton::clicked, this, &DrillControlPage::onPrInitClicked);
    connect(ui->btn_Pr_start, &QPushButton::clicked, this, &DrillControlPage::onPrStartClicked);
    connect(ui->btn_Pr_stop, &QPushButton::clicked, this, &DrillControlPage::onPrStopClicked);

    // Pi - 冲击
    connect(ui->btn_Pi_unlock, &QPushButton::clicked, this, &DrillControlPage::onPiUnlockClicked);
    connect(ui->btn_Pi_start, &QPushButton::clicked, this, &DrillControlPage::onPiStartClicked);
    connect(ui->btn_Pi_stop, &QPushButton::clicked, this, &DrillControlPage::onPiStopClicked);

    // Cb - 夹紧
    connect(ui->btn_Cb_init, &QPushButton::clicked, this, &DrillControlPage::onCbInitClicked);
    connect(ui->btn_Cb_open, &QPushButton::clicked, this, &DrillControlPage::onCbOpenClicked);
    connect(ui->btn_Cb_close, &QPushButton::clicked, this, &DrillControlPage::onCbCloseClicked);

    // Sr - 料仓
    connect(ui->btn_Sr_init, &QPushButton::clicked, this, &DrillControlPage::onSrInitClicked);
    connect(ui->btn_Sr_prev, &QPushButton::clicked, this, &DrillControlPage::onSrPrevClicked);
    connect(ui->btn_Sr_next, &QPushButton::clicked, this, &DrillControlPage::onSrNextClicked);
    connect(ui->btn_Sr_goto, &QPushButton::clicked, this, &DrillControlPage::onSrGotoClicked);

    // Dh - 对接
    connect(ui->btn_Dh_connect, &QPushButton::clicked, this, &DrillControlPage::onDhConnectClicked);
    connect(ui->btn_Dh_extend, &QPushButton::clicked, this, &DrillControlPage::onDhExtendClicked);
    connect(ui->btn_Dh_retract, &QPushButton::clicked, this, &DrillControlPage::onDhRetractClicked);

    // Me - 机械手伸缩
    connect(ui->btn_Me_init, &QPushButton::clicked, this, &DrillControlPage::onMeInitClicked);
    connect(ui->btn_Me_move, &QPushButton::clicked, this, &DrillControlPage::onMeMoveClicked);
    connect(ui->btn_Me_extend, &QPushButton::clicked, this, &DrillControlPage::onMeExtendClicked);
    connect(ui->btn_Me_retract, &QPushButton::clicked, this, &DrillControlPage::onMeRetractClicked);

    // Mg - 机械手夹紧
    connect(ui->btn_Mg_init, &QPushButton::clicked, this, &DrillControlPage::onMgInitClicked);
    connect(ui->btn_Mg_open, &QPushButton::clicked, this, &DrillControlPage::onMgOpenClicked);
    connect(ui->btn_Mg_close, &QPushButton::clicked, this, &DrillControlPage::onMgCloseClicked);

    // Mr - 机械手回转
    connect(ui->btn_Mr_init, &QPushButton::clicked, this, &DrillControlPage::onMrInitClicked);
    connect(ui->btn_Mr_move, &QPushButton::clicked, this, &DrillControlPage::onMrMoveClicked);
    connect(ui->btn_Mr_drill, &QPushButton::clicked, this, &DrillControlPage::onMrDrillClicked);
    connect(ui->btn_Mr_storage, &QPushButton::clicked, this, &DrillControlPage::onMrStorageClicked);

    // 配置编辑
    connect(ui->combo_mechanism, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DrillControlPage::onMechanismSelectionChanged);
    connect(ui->btn_apply_config, &QPushButton::clicked, this, &DrillControlPage::onApplyConfigClicked);
    connect(ui->btn_save_config, &QPushButton::clicked, this, &DrillControlPage::onSaveConfigClicked);
    connect(ui->btn_reset_config, &QPushButton::clicked, this, &DrillControlPage::onResetConfigClicked);

    // 配置热更新 - 监听配置管理器的变化信号
    connect(MotionConfigManager::instance(), &MotionConfigManager::mechanismConfigChanged,
            this, &DrillControlPage::onMechanismConfigChanged);
}

void DrillControlPage::loadConfig()
{
    auto* configMgr = MotionConfigManager::instance();

    // 尝试多个配置文件路径
    QStringList configPaths = {
        QCoreApplication::applicationDirPath() + "/config/mechanisms.json",  // 可执行文件目录
        QCoreApplication::applicationDirPath() + "/../config/mechanisms.json",  // 上级目录
        "D:/KT_DrillControl/config/mechanisms.json",  // 绝对路径（开发环境）
        "../config/mechanisms.json",  // 相对路径
        "config/mechanisms.json"  // 当前目录
    };

    for (const QString& configPath : configPaths) {
        if (QFile::exists(configPath)) {
            if (configMgr->loadConfig(configPath)) {
                appendLog("Configuration loaded: " + configPath);
                return;
            }
        }
    }

    appendLog("Failed to load config from any path, using defaults");
}

void DrillControlPage::createControllers()
{
    if (!m_driver) {
        appendLog("Cannot create controllers: no driver");
        return;
    }

    // 配置已在构造函数中加载，无需重复加载
    auto* configMgr = MotionConfigManager::instance();

    // 创建各控制器
    m_feedController = new FeedController(m_driver, configMgr->getPenetrationConfig(), this);
    m_rotationController = new RotationController(m_driver, configMgr->getRotationConfig(), this);
    m_percussionController = new PercussionController(m_driver, configMgr->getPercussionConfig(), this);
    m_clampController = new ClampController(m_driver, configMgr->getClampConfig(), this);
    m_storageController = new StorageController(m_driver, configMgr->getStorageConfig(), this);
    m_armExtController = new ArmExtensionController(m_driver, configMgr->getArmExtensionConfig(), this);
    m_armGripController = new ArmGripController(m_driver, configMgr->getArmGripConfig(), this);
    m_armRotController = new ArmRotationController(m_driver, configMgr->getArmRotationConfig(), this);

    // Dh使用Modbus，单独创建
    m_dockingController = new DockingController(configMgr->getDockingConfig(), this);

    appendLog("All controllers created");

    // 通知其他页面控制器已准备就绪
    emit controllersReady();
}

void DrillControlPage::destroyControllers()
{
    delete m_feedController; m_feedController = nullptr;
    delete m_rotationController; m_rotationController = nullptr;
    delete m_percussionController; m_percussionController = nullptr;
    delete m_clampController; m_clampController = nullptr;
    delete m_storageController; m_storageController = nullptr;
    delete m_dockingController; m_dockingController = nullptr;
    delete m_armExtController; m_armExtController = nullptr;
    delete m_armGripController; m_armGripController = nullptr;
    delete m_armRotController; m_armRotController = nullptr;
}

// ============================================================================
// 系统控制槽函数
// ============================================================================

void DrillControlPage::onConnectClicked()
{
    if (m_isConnected) {
        // 断开连接
        m_statusTimer->stop();
        destroyControllers();

        if (m_ownsDriver && m_driver) {
            ZMotionDriver* zDriver = dynamic_cast<ZMotionDriver*>(m_driver);
            if (zDriver) {
                zDriver->disconnect();
            }
            delete m_driver;
            m_driver = nullptr;
        }

        m_isConnected = false;
        m_ownsDriver = false;
        setStatusLabel(ui->lbl_connection_status, "● 未连接", "#909399");
        ui->btn_connect->setText("连接控制器");
        appendLog("Disconnected");
    } else {
        // 连接
        ZMotionDriver* zDriver = new ZMotionDriver();

        if (zDriver->connect("192.168.0.11")) {
            m_driver = zDriver;
            m_ownsDriver = true;
            m_isConnected = true;

            createControllers();

            setStatusLabel(ui->lbl_connection_status, "● 已连接", "#67c23a");
            ui->btn_connect->setText("断开连接");
            m_statusTimer->start(200);
            appendLog("Connected to ZMotion controller");
        } else {
            appendLog("Failed to connect to controller");
            delete zDriver;
        }
    }
}

void DrillControlPage::onInitAllClicked()
{
    appendLog("Initializing all mechanisms...");

    if (m_feedController) m_feedController->initialize();
    if (m_rotationController) m_rotationController->initialize();
    if (m_percussionController) m_percussionController->initialize();
    if (m_clampController) m_clampController->initialize();
    if (m_storageController) m_storageController->initialize();
    if (m_armExtController) m_armExtController->initialize();
    if (m_armGripController) m_armGripController->initialize();
    if (m_armRotController) m_armRotController->initialize();

    appendLog("All ZMotion mechanisms initialized");
}

void DrillControlPage::onStopAllClicked()
{
    appendLog("EMERGENCY STOP - All mechanisms");

    if (m_feedController) m_feedController->stop();
    if (m_rotationController) m_rotationController->stop();
    if (m_percussionController) m_percussionController->stop();
    if (m_clampController) m_clampController->stop();
    if (m_storageController) m_storageController->stop();
    if (m_dockingController) m_dockingController->stop();
    if (m_armExtController) m_armExtController->stop();
    if (m_armGripController) m_armGripController->stop();
    if (m_armRotController) m_armRotController->stop();
}

void DrillControlPage::onReloadConfigClicked()
{
    loadConfig();
    appendLog("Configuration reloaded");
}

// ============================================================================
// Fz - 进给控制
// ============================================================================

void DrillControlPage::onFzInitClicked()
{
    if (m_feedController) {
        appendLog("[Fz] Initializing...");
        m_feedController->initialize();
    }
}

void DrillControlPage::onFzMoveClicked()
{
    if (m_feedController) {
        bool ok;
        double target = ui->le_Fz_target->text().toDouble(&ok);
        if (ok) {
            appendLog(QString("[Fz] Moving to %1 mm").arg(target));
            m_feedController->setTargetDepth(target);
        } else {
            appendLog("[Fz] Invalid target value");
        }
    }
}

void DrillControlPage::onFzSafeClicked()
{
    if (m_feedController) {
        appendLog("[Fz] Moving to safe position");
        m_feedController->gotoSafePosition();
    }
}

void DrillControlPage::onFzStopClicked()
{
    if (m_feedController) {
        appendLog("[Fz] Stop");
        m_feedController->stop();
    }
}

// ============================================================================
// Pr - 回转控制
// ============================================================================

void DrillControlPage::onPrInitClicked()
{
    if (m_rotationController) {
        appendLog("[Pr] Initializing...");
        m_rotationController->initialize();
    }
}

void DrillControlPage::onPrStartClicked()
{
    if (m_rotationController) {
        bool ok;
        double rpm = ui->le_Pr_rpm->text().toDouble(&ok);
        if (ok && rpm > 0) {
            appendLog(QString("[Pr] Starting rotation at %1 rpm").arg(rpm));
            m_rotationController->startRotation(rpm);
        } else {
            appendLog("[Pr] Starting rotation at default speed");
            m_rotationController->startRotation();
        }
    }
}

void DrillControlPage::onPrStopClicked()
{
    if (m_rotationController) {
        appendLog("[Pr] Stop");
        m_rotationController->stopRotation();
    }
}

// ============================================================================
// Pi - 冲击控制
// ============================================================================

void DrillControlPage::onPiUnlockClicked()
{
    if (m_percussionController) {
        appendLog("[Pi] Unlocking...");
        m_percussionController->unlock();
    }
}

void DrillControlPage::onPiStartClicked()
{
    if (m_percussionController) {
        bool ok;
        double freq = ui->le_Pi_freq->text().toDouble(&ok);
        if (ok && freq > 0) {
            appendLog(QString("[Pi] Starting percussion at %1 Hz").arg(freq));
            m_percussionController->startPercussion(freq);
        } else {
            appendLog("[Pi] Starting percussion at default frequency");
            m_percussionController->startPercussion();
        }
    }
}

void DrillControlPage::onPiStopClicked()
{
    if (m_percussionController) {
        appendLog("[Pi] Stop");
        m_percussionController->stopPercussion();
    }
}

// ============================================================================
// Cb - 夹紧控制
// ============================================================================

void DrillControlPage::onCbInitClicked()
{
    if (m_clampController) {
        appendLog("[Cb] Initializing...");
        m_clampController->initializeClamp();
    }
}

void DrillControlPage::onCbOpenClicked()
{
    if (m_clampController) {
        appendLog("[Cb] Opening");
        m_clampController->open();
    }
}

void DrillControlPage::onCbCloseClicked()
{
    if (m_clampController) {
        bool ok;
        double torque = ui->le_Cb_torque->text().toDouble(&ok);
        if (ok && torque > 0) {
            appendLog(QString("[Cb] Closing with torque %1").arg(torque));
            m_clampController->close(torque);
        } else {
            appendLog("[Cb] Closing with default torque");
            m_clampController->close();
        }
    }
}

// ============================================================================
// Sr - 料仓控制
// ============================================================================

void DrillControlPage::onSrInitClicked()
{
    if (m_storageController) {
        appendLog("[Sr] Initializing...");
        m_storageController->initialize();
    }
}

void DrillControlPage::onSrPrevClicked()
{
    if (m_storageController) {
        appendLog("[Sr] Moving to previous position");
        m_storageController->moveBackward();
    }
}

void DrillControlPage::onSrNextClicked()
{
    if (m_storageController) {
        appendLog("[Sr] Moving to next position");
        m_storageController->moveForward();
    }
}

void DrillControlPage::onSrGotoClicked()
{
    if (m_storageController) {
        bool ok;
        int pos = ui->le_Sr_target->text().toInt(&ok);
        if (ok && pos >= 0 && pos <= 6) {
            appendLog(QString("[Sr] Moving to position %1").arg(pos));
            m_storageController->moveToPosition(pos);
        } else {
            appendLog("[Sr] Invalid position (0-6)");
        }
    }
}

// ============================================================================
// Dh - 对接控制
// ============================================================================

void DrillControlPage::onDhConnectClicked()
{
    if (m_dockingController) {
        if (m_dockingController->isConnected()) {
            appendLog("[Dh] Disconnecting...");
            m_dockingController->disconnect();
        } else {
            appendLog("[Dh] Connecting...");
            if (m_dockingController->connect()) {
                appendLog("[Dh] Connected");
            } else {
                appendLog("[Dh] Connection failed");
            }
        }
    }
}

void DrillControlPage::onDhExtendClicked()
{
    if (m_dockingController) {
        appendLog("[Dh] Extending");
        m_dockingController->extend();
    }
}

void DrillControlPage::onDhRetractClicked()
{
    if (m_dockingController) {
        appendLog("[Dh] Retracting");
        m_dockingController->retract();
    }
}

// ============================================================================
// Me - 机械手伸缩控制
// ============================================================================

void DrillControlPage::onMeInitClicked()
{
    if (m_armExtController) {
        appendLog("[Me] Initializing...");
        m_armExtController->initializePosition();
    }
}

void DrillControlPage::onMeMoveClicked()
{
    if (m_armExtController) {
        bool ok;
        double target = ui->le_Me_target->text().toDouble(&ok);
        if (ok) {
            appendLog(QString("[Me] Moving to %1").arg(target));
            m_armExtController->setPosition(target);
        } else {
            appendLog("[Me] Invalid target value");
        }
    }
}

void DrillControlPage::onMeExtendClicked()
{
    if (m_armExtController) {
        appendLog("[Me] Extending");
        m_armExtController->extend();
    }
}

void DrillControlPage::onMeRetractClicked()
{
    if (m_armExtController) {
        appendLog("[Me] Retracting");
        m_armExtController->retract();
    }
}

// ============================================================================
// Mg - 机械手夹紧控制
// ============================================================================

void DrillControlPage::onMgInitClicked()
{
    if (m_armGripController) {
        appendLog("[Mg] Initializing...");
        m_armGripController->initializeGrip();
    }
}

void DrillControlPage::onMgOpenClicked()
{
    if (m_armGripController) {
        appendLog("[Mg] Opening");
        m_armGripController->open();
    }
}

void DrillControlPage::onMgCloseClicked()
{
    if (m_armGripController) {
        bool ok;
        double torque = ui->le_Mg_torque->text().toDouble(&ok);
        if (ok && torque > 0) {
            appendLog(QString("[Mg] Closing with torque %1").arg(torque));
            m_armGripController->close(torque);
        } else {
            appendLog("[Mg] Closing with default torque");
            m_armGripController->close();
        }
    }
}

// ============================================================================
// Mr - 机械手回转控制
// ============================================================================

void DrillControlPage::onMrInitClicked()
{
    if (m_armRotController) {
        appendLog("[Mr] Initializing...");
        m_armRotController->initialize();
    }
}

void DrillControlPage::onMrMoveClicked()
{
    if (m_armRotController) {
        bool ok;
        double angle = ui->le_Mr_angle->text().toDouble(&ok);
        if (ok) {
            appendLog(QString("[Mr] Moving to %1°").arg(angle));
            m_armRotController->setAngle(angle);
        } else {
            appendLog("[Mr] Invalid angle value");
        }
    }
}

void DrillControlPage::onMrDrillClicked()
{
    if (m_armRotController) {
        appendLog("[Mr] Moving to drill position");
        m_armRotController->rotateToDrill();
    }
}

void DrillControlPage::onMrStorageClicked()
{
    if (m_armRotController) {
        appendLog("[Mr] Moving to storage position");
        m_armRotController->rotateToStorage();
    }
}

// ============================================================================
// 状态更新
// ============================================================================

void DrillControlPage::updateStatus()
{
    updateFzStatus();
    updatePrStatus();
    updatePiStatus();
    updateCbStatus();
    updateSrStatus();
    updateDhStatus();
    updateMeStatus();
    updateMgStatus();
    updateMrStatus();
}

void DrillControlPage::updateFzStatus()
{
    if (!m_feedController) return;

    m_feedController->updateStatus();
    double depth = m_feedController->currentDepth();
    ui->lbl_Fz_pos->setText(QString("位置: %1 mm").arg(depth, 0, 'f', 2));

    if (m_feedController->isReady()) {
        setStatusLabel(ui->lbl_Fz_status, "就绪", "#67c23a");
    } else if (m_feedController->state() == MechanismState::Moving) {
        setStatusLabel(ui->lbl_Fz_status, "运动中", "#409eff");
    }
}

void DrillControlPage::updatePrStatus()
{
    if (!m_rotationController) return;

    m_rotationController->updateStatus();

    if (m_rotationController->isRotating()) {
        double rpm = m_rotationController->actualSpeed();
        ui->lbl_Pr_rpm->setText(QString("转速: %1 rpm").arg(rpm, 0, 'f', 0));
        setStatusLabel(ui->lbl_Pr_status, "运行中", "#67c23a");
    } else if (m_rotationController->isReady()) {
        ui->lbl_Pr_rpm->setText("转速: 0 rpm");
        setStatusLabel(ui->lbl_Pr_status, "就绪", "#409eff");
    }
}

void DrillControlPage::updatePiStatus()
{
    if (!m_percussionController) return;

    m_percussionController->updateStatus();

    if (m_percussionController->isLocked()) {
        setStatusLabel(ui->lbl_Pi_status, "锁定", "#909399");
    } else if (m_percussionController->isPercussing()) {
        double freq = m_percussionController->frequency();
        ui->lbl_Pi_freq->setText(QString("频率: %1 Hz").arg(freq, 0, 'f', 1));
        setStatusLabel(ui->lbl_Pi_status, "运行中", "#67c23a");
    } else {
        setStatusLabel(ui->lbl_Pi_status, "已解锁", "#409eff");
    }
}

void DrillControlPage::updateCbStatus()
{
    if (!m_clampController) return;

    ClampState state = m_clampController->state();
    QString stateStr;

    switch (state) {
        case ClampState::Open: stateStr = "张开"; break;
        case ClampState::Closed: stateStr = "夹紧"; break;
        case ClampState::Opening: stateStr = "张开中"; break;
        case ClampState::Closing: stateStr = "夹紧中"; break;
        default: stateStr = "未知"; break;
    }

    ui->lbl_Cb_state->setText("状态: " + stateStr);

    if (m_clampController->isReady()) {
        setStatusLabel(ui->lbl_Cb_status, "就绪", "#67c23a");
    }
}

void DrillControlPage::updateSrStatus()
{
    if (!m_storageController) return;

    int pos = m_storageController->currentPosition();
    ui->lbl_Sr_pos->setText(QString("位置: %1/7").arg(pos));

    if (m_storageController->isReady()) {
        setStatusLabel(ui->lbl_Sr_status, "就绪", "#67c23a");
    } else if (m_storageController->state() == MechanismState::Moving) {
        setStatusLabel(ui->lbl_Sr_status, "运动中", "#409eff");
    }
}

void DrillControlPage::updateDhStatus()
{
    if (!m_dockingController) return;

    if (m_dockingController->isConnected()) {
        DockingState state = m_dockingController->dockingState();
        QString stateStr;

        switch (state) {
            case DockingState::Extended: stateStr = "已伸出"; break;
            case DockingState::Retracted: stateStr = "已收回"; break;
            case DockingState::Moving: stateStr = "运动中"; break;
            default: stateStr = "未知"; break;
        }

        ui->lbl_Dh_state->setText("状态: " + stateStr);
        setStatusLabel(ui->lbl_Dh_status, "在线", "#67c23a");
        ui->btn_Dh_connect->setText("断开");
    } else {
        ui->lbl_Dh_state->setText("状态: 离线");
        setStatusLabel(ui->lbl_Dh_status, "离线", "#909399");
        ui->btn_Dh_connect->setText("连接");
    }
}

void DrillControlPage::updateMeStatus()
{
    if (!m_armExtController) return;

    m_armExtController->updateStatus();
    double pos = m_armExtController->currentPosition();
    ui->lbl_Me_pos->setText(QString("位置: %1").arg(pos, 0, 'f', 2));

    if (m_armExtController->isReady()) {
        setStatusLabel(ui->lbl_Me_status, "就绪", "#67c23a");
    } else if (m_armExtController->isMoving()) {
        setStatusLabel(ui->lbl_Me_status, "运动中", "#409eff");
    }
}

void DrillControlPage::updateMgStatus()
{
    if (!m_armGripController) return;

    ClampState state = m_armGripController->clampState();
    QString stateStr;

    switch (state) {
        case ClampState::Open: stateStr = "张开"; break;
        case ClampState::Closed: stateStr = "夹紧"; break;
        case ClampState::Opening: stateStr = "张开中"; break;
        case ClampState::Closing: stateStr = "夹紧中"; break;
        default: stateStr = "未知"; break;
    }

    ui->lbl_Mg_state->setText("状态: " + stateStr);

    if (m_armGripController->isReady()) {
        setStatusLabel(ui->lbl_Mg_status, "就绪", "#67c23a");
    }
}

void DrillControlPage::updateMrStatus()
{
    if (!m_armRotController) return;

    m_armRotController->updateStatus();
    double angle = m_armRotController->currentAngle();
    ui->lbl_Mr_angle->setText(QString("角度: %1°").arg(angle, 0, 'f', 1));

    if (m_armRotController->isReady()) {
        setStatusLabel(ui->lbl_Mr_status, "就绪", "#67c23a");
    } else if (m_armRotController->isRotating()) {
        setStatusLabel(ui->lbl_Mr_status, "运动中", "#409eff");
    }
}

// ============================================================================
// 辅助函数
// ============================================================================

void DrillControlPage::setStatusLabel(QLabel* label, const QString& text, const QString& color)
{
    if (label) {
        label->setText(text);
        label->setStyleSheet(QString("background-color: %1; color: white; padding: 2px 6px; border-radius: 3px;").arg(color));
    }
}

void DrillControlPage::appendLog(const QString& message)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    QString logLine = QString("[%1] %2").arg(timestamp).arg(message);

    ui->tb_log->append(logLine);
    emit logMessage(logLine);

    qDebug() << "[DrillControlPage]" << message;
}

void DrillControlPage::onControllerStateChanged(int state, const QString& message)
{
    appendLog(QString("State changed: %1 - %2").arg(state).arg(message));
}

void DrillControlPage::onControllerError(const QString& error)
{
    appendLog(QString("ERROR: %1").arg(error));
}

// ============================================================================
// 配置编辑Tab
// ============================================================================

void DrillControlPage::setupConfigTab()
{
    // 填充机构选择下拉框
    ui->combo_mechanism->clear();
    for (int i = 0; i < Mechanism::COUNT; ++i) {
        Mechanism::Code code = static_cast<Mechanism::Code>(i);
        QString displayName = QString("%1 - %2")
            .arg(Mechanism::getCodeString(code))
            .arg(Mechanism::getNameCN(code));
        ui->combo_mechanism->addItem(displayName, i);
    }

    // 默认选择第一个机构并加载配置
    if (ui->combo_mechanism->count() > 0) {
        ui->combo_mechanism->setCurrentIndex(0);
        onMechanismSelectionChanged(0);
    }
}

void DrillControlPage::onMechanismSelectionChanged(int index)
{
    if (index < 0 || index >= Mechanism::COUNT) return;

    m_currentConfigMechanism = static_cast<Mechanism::Code>(index);
    loadMechanismToUI(m_currentConfigMechanism);
    updateConfigGroupVisibility(m_currentConfigMechanism);

    appendLog(QString("Config: Selected %1").arg(Mechanism::getCodeString(m_currentConfigMechanism)));
}

void DrillControlPage::loadMechanismToUI(Mechanism::Code code)
{
    auto* configMgr = MotionConfigManager::instance();
    MechanismParams params = configMgr->getMechanismConfig(code);

    // 运动参数
    ui->spin_speed->setValue(params.speed);
    ui->spin_acceleration->setValue(params.acceleration);
    ui->spin_deceleration->setValue(params.deceleration);

    // 位置参数
    ui->spin_max_position->setValue(params.maxPosition);
    ui->spin_min_position->setValue(params.minPosition);
    ui->spin_safe_position->setValue(params.safePosition);
    ui->spin_work_position->setValue(params.workPosition);

    // 力矩参数
    ui->spin_open_dac->setValue(params.openDac);
    ui->spin_close_dac->setValue(params.closeDac);
    ui->spin_init_dac->setValue(params.initDac);

    // 转换系数
    ui->spin_pulses_per_mm->setValue(params.pulsesPerMm);
    ui->spin_pulses_per_degree->setValue(params.pulsesPerDegree);

    // 填充关键位置表格
    loadKeyPositionsToTable(code, params);
}

void DrillControlPage::updateConfigGroupVisibility(Mechanism::Code code)
{
    // 获取控制模式
    int mode = Mechanism::getDefaultMode(code);
    bool isPosition = (mode == Mechanism::Mode::Position);
    bool isTorque = (mode == Mechanism::Mode::Torque);
    bool isVelocity = (mode == Mechanism::Mode::Velocity);
    bool isModbus = Mechanism::isModbus(code);

    // 位置参数：位置模式机构显示
    ui->group_position_params->setVisible(isPosition);

    // 力矩参数：力矩模式机构显示
    ui->group_torque_params->setVisible(isTorque);

    // 转换系数：位置模式机构显示
    ui->group_conversion_params->setVisible(isPosition || isVelocity);

    // 运动参数：所有非Modbus机构显示
    ui->group_motion_params->setVisible(!isModbus);
}

void DrillControlPage::onApplyConfigClicked()
{
    auto* configMgr = MotionConfigManager::instance();

    // 从UI读取参数
    MechanismParams params = configMgr->getMechanismConfig(m_currentConfigMechanism);

    // 更新运动参数
    params.speed = ui->spin_speed->value();
    params.acceleration = ui->spin_acceleration->value();
    params.deceleration = ui->spin_deceleration->value();

    // 更新位置参数
    params.maxPosition = ui->spin_max_position->value();
    params.minPosition = ui->spin_min_position->value();
    params.safePosition = ui->spin_safe_position->value();
    params.workPosition = ui->spin_work_position->value();

    // 更新力矩参数
    params.openDac = ui->spin_open_dac->value();
    params.closeDac = ui->spin_close_dac->value();
    params.initDac = ui->spin_init_dac->value();

    // 更新转换系数
    params.pulsesPerMm = ui->spin_pulses_per_mm->value();
    params.pulsesPerDegree = ui->spin_pulses_per_degree->value();

    // 从表格读取关键位置
    saveKeyPositionsFromTable(params);

    // 更新配置管理器
    configMgr->updateMechanismConfig(m_currentConfigMechanism, params);

    appendLog(QString("Config: Applied to %1").arg(Mechanism::getCodeString(m_currentConfigMechanism)));
}

void DrillControlPage::onSaveConfigClicked()
{
    // 先应用当前修改
    onApplyConfigClicked();

    auto* configMgr = MotionConfigManager::instance();
    QString configPath = configMgr->configFilePath();

    if (configMgr->saveConfig(configPath)) {
        appendLog("Config: Saved to " + configPath);
    } else {
        appendLog("Config: Failed to save");
    }
}

void DrillControlPage::onResetConfigClicked()
{
    // 重新从配置管理器加载当前机构配置
    loadMechanismToUI(m_currentConfigMechanism);
    appendLog(QString("Config: Reset %1").arg(Mechanism::getCodeString(m_currentConfigMechanism)));
}

// ============================================================================
// 配置热更新
// ============================================================================

void DrillControlPage::onMechanismConfigChanged(Mechanism::Code code)
{
    auto* configMgr = MotionConfigManager::instance();
    appendLog(QString("Config: Hot-reload %1").arg(Mechanism::getCodeString(code)));

    // 根据机构代号更新对应的控制器配置
    switch (code) {
    case Mechanism::Fz:
        if (m_feedController) {
            m_feedController->updateConfig(configMgr->getPenetrationConfig());
        }
        break;
    case Mechanism::Pr:
        if (m_rotationController) {
            m_rotationController->updateConfig(configMgr->getRotationConfig());
        }
        break;
    case Mechanism::Pi:
        if (m_percussionController) {
            m_percussionController->updateConfig(configMgr->getPercussionConfig());
        }
        break;
    case Mechanism::Cb:
        if (m_clampController) {
            m_clampController->updateConfig(configMgr->getClampConfig());
        }
        break;
    case Mechanism::Sr:
        if (m_storageController) {
            m_storageController->updateConfig(configMgr->getStorageConfig());
        }
        break;
    case Mechanism::Dh:
        if (m_dockingController) {
            m_dockingController->updateConfig(configMgr->getDockingConfig());
        }
        break;
    case Mechanism::Me:
        if (m_armExtController) {
            m_armExtController->updateConfig(configMgr->getArmExtensionConfig());
        }
        break;
    case Mechanism::Mg:
        if (m_armGripController) {
            m_armGripController->updateConfig(configMgr->getArmGripConfig());
        }
        break;
    case Mechanism::Mr:
        if (m_armRotController) {
            m_armRotController->updateConfig(configMgr->getArmRotationConfig());
        }
        break;
    default:
        break;
    }

    // 如果当前配置编辑页面正在编辑这个机构，刷新UI
    if (m_currentConfigMechanism == code) {
        loadMechanismToUI(code);
    }
}

// ============================================================================
// 关键位置表格操作
// ============================================================================

void DrillControlPage::loadKeyPositionsToTable(Mechanism::Code code, const MechanismParams& params)
{
    QTableWidget* table = ui->table_key_positions;
    table->setRowCount(0);

    // 获取该机构的关键位置元数据
    if (!s_keyPositionMeta.contains(code)) {
        return;
    }

    const QVector<KeyPositionInfo>& meta = s_keyPositionMeta[code];
    table->setRowCount(meta.size());

    // 设置表格列宽
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    for (int row = 0; row < meta.size(); ++row) {
        const KeyPositionInfo& info = meta[row];

        // 位置代号 (只读)
        QTableWidgetItem* keyItem = new QTableWidgetItem(info.key);
        keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
        keyItem->setTextAlignment(Qt::AlignCenter);
        table->setItem(row, 0, keyItem);

        // 位置说明 (只读)
        QTableWidgetItem* descItem = new QTableWidgetItem(info.description);
        descItem->setFlags(descItem->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 1, descItem);

        // 值 (可编辑)
        double value = params.getKeyPosition(info.key, 0.0);
        QTableWidgetItem* valueItem = new QTableWidgetItem(QString::number(value, 'f', 1));
        valueItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table->setItem(row, 2, valueItem);
    }
}

void DrillControlPage::saveKeyPositionsFromTable(MechanismParams& params)
{
    QTableWidget* table = ui->table_key_positions;

    // 获取该机构的关键位置元数据
    if (!s_keyPositionMeta.contains(m_currentConfigMechanism)) {
        return;
    }

    const QVector<KeyPositionInfo>& meta = s_keyPositionMeta[m_currentConfigMechanism];

    for (int row = 0; row < table->rowCount() && row < meta.size(); ++row) {
        QTableWidgetItem* valueItem = table->item(row, 2);
        if (valueItem) {
            bool ok;
            double value = valueItem->text().toDouble(&ok);
            if (ok) {
                params.setKeyPosition(meta[row].key, value);
            }
        }
    }
}
