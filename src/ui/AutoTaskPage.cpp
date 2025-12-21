#include "ui/AutoTaskPage.h"
#include "ui_AutoTaskPage.h"

#include "control/FeedController.h"
#include "control/RotationController.h"
#include "control/PercussionController.h"
#include "control/AcquisitionManager.h"
#include "dataACQ/MdbWorker.h"
#include "dataACQ/MotorWorker.h"

#include <QFileDialog>
#include <QDir>
#include <QDateTime>
#include <QTimer>
#include <QMessageBox>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>

#ifdef ENABLE_TEST_MODE
#include "MockDataGenerator.h"
#include <QPushButton>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

// 声明外部测试函数
extern void testAutoTask();
#endif

AutoTaskPage::AutoTaskPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::AutoTaskPage)
    , m_feedController(nullptr)
    , m_rotationController(nullptr)
    , m_percussionController(nullptr)
    , m_acquisitionManager(nullptr)
    , m_drillManager(nullptr)
    , m_elapsedTimer(new QTimer(this))
    , m_tasksDirectory("config/auto_tasks")
#ifdef ENABLE_TEST_MODE
    , m_mockGenerator(nullptr)
#endif
{
    ui->setupUi(this);
    setupConnections();

    // Setup elapsed timer (update every second)
    m_elapsedTimer->setInterval(1000);
    connect(m_elapsedTimer, &QTimer::timeout,
            this, &AutoTaskPage::onElapsedTimerTick);

    // Setup steps table
    ui->table_steps->setColumnWidth(0, 40);   // #
    ui->table_steps->setColumnWidth(1, 80);   // 类型
    ui->table_steps->setColumnWidth(2, 100);  // 目标
    ui->table_steps->setColumnWidth(3, 60);   // 预设
    ui->table_steps->setColumnWidth(4, 80);   // 状态

    // Setup presets table
    ui->table_presets->setColumnWidth(0, 50);  // ID
    ui->table_presets->setColumnWidth(1, 80);  // Vp
    ui->table_presets->setColumnWidth(2, 70);  // RPM
    ui->table_presets->setColumnWidth(3, 70);  // Fi

    updateUIState();
    loadTasksFromDirectory();

#ifdef ENABLE_TEST_MODE
    setupTestUI();
#endif
}

AutoTaskPage::~AutoTaskPage()
{
    if (m_drillManager) {
        m_drillManager->abort();
    }
#ifdef ENABLE_TEST_MODE
    if (m_mockGenerator) {
        m_mockGenerator->stopSimulation();
    }
#endif
    delete ui;
}

void AutoTaskPage::setControllers(FeedController* feed,
                                   RotationController* rotation,
                                   PercussionController* percussion)
{
    m_feedController = feed;
    m_rotationController = rotation;
    m_percussionController = percussion;

    // Create drill manager with controllers
    if (m_feedController && m_rotationController && m_percussionController) {
        if (m_drillManager) {
            delete m_drillManager;
        }

        m_drillManager = new AutoDrillManager(m_feedController,
                                              m_rotationController,
                                              m_percussionController,
                                              this);

        // Connect drill manager signals
        connect(m_drillManager, &AutoDrillManager::stateChanged,
                this, &AutoTaskPage::onTaskStateChanged);
        connect(m_drillManager, &AutoDrillManager::stepStarted,
                this, &AutoTaskPage::onStepStarted);
        connect(m_drillManager, &AutoDrillManager::stepCompleted,
                this, &AutoTaskPage::onStepCompleted);
        connect(m_drillManager, &AutoDrillManager::progressUpdated,
                this, &AutoTaskPage::onProgressUpdated);
        connect(m_drillManager, &AutoDrillManager::taskCompleted,
                this, &AutoTaskPage::onTaskCompleted);
        connect(m_drillManager, &AutoDrillManager::taskFailed,
                this, &AutoTaskPage::onTaskFailed);
        connect(m_drillManager, &AutoDrillManager::logMessage,
                this, &AutoTaskPage::onLogMessage);

        if (m_acquisitionManager) {
            m_drillManager->setDataWorkers(
                m_acquisitionManager->mdbWorker(),
                m_acquisitionManager->motorWorker());
        }

#ifdef ENABLE_TEST_MODE
        // 连接 MockDataGenerator 到新创建的 AutoDrillManager
        if (m_mockGenerator) {
            connect(m_mockGenerator, &MockDataGenerator::dataBlockReady,
                    m_drillManager, &AutoDrillManager::onDataBlockReceived,
                    Qt::UniqueConnection);  // 防止重复连接
        }
#endif
    }
}

void AutoTaskPage::setAcquisitionManager(AcquisitionManager* manager)
{
    if (!manager) {
        return;
    }

    m_acquisitionManager = manager;

    // 连接采集状态变化信号
    connect(m_acquisitionManager, &AcquisitionManager::acquisitionStateChanged,
            this, &AutoTaskPage::logAcquisitionEvent, Qt::UniqueConnection);

    // 如果drillManager已创建，将数据worker连接到它
    if (m_drillManager) {
        m_drillManager->setDataWorkers(manager->mdbWorker(), manager->motorWorker());
        appendLog(tr("数据采集已连接"));
    }
}

void AutoTaskPage::setupConnections()
{
    // Top bar buttons
    connect(ui->btn_load_task, &QPushButton::clicked,
            this, &AutoTaskPage::onLoadTaskClicked);
    connect(ui->btn_reload, &QPushButton::clicked,
            this, &AutoTaskPage::onReloadClicked);

    // Task list
    connect(ui->btn_import_task, &QPushButton::clicked,
            this, &AutoTaskPage::onImportTaskClicked);
    connect(ui->list_tasks, &QListWidget::itemClicked,
            this, &AutoTaskPage::onTaskListItemClicked);
    connect(ui->list_tasks, &QListWidget::itemDoubleClicked,
            this, &AutoTaskPage::onTaskListItemDoubleClicked);

    // Control buttons
    connect(ui->btn_start, &QPushButton::clicked,
            this, &AutoTaskPage::onStartClicked);
    connect(ui->btn_pause, &QPushButton::clicked,
            this, &AutoTaskPage::onPauseClicked);
    connect(ui->btn_resume, &QPushButton::clicked,
            this, &AutoTaskPage::onResumeClicked);
    connect(ui->btn_stop, &QPushButton::clicked,
            this, &AutoTaskPage::onStopClicked);
    connect(ui->btn_emergency, &QPushButton::clicked,
            this, &AutoTaskPage::onEmergencyClicked);
}

void AutoTaskPage::onLoadTaskClicked()
{
    QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("选择任务文件"),
        m_tasksDirectory,
        tr("JSON 文件 (*.json);;所有文件 (*.*)"));

    if (fileName.isEmpty()) {
        return;
    }

    if (!m_drillManager) {
        QMessageBox::warning(this, tr("错误"), tr("钻进管理器未初始化"));
        return;
    }

    if (m_drillManager->loadTaskFile(fileName)) {
        m_currentTaskFile = fileName;
        updateStepsTable();
        updatePresetsTable();
        appendLog(tr("任务文件已加载: %1").arg(fileName));
    }
}

void AutoTaskPage::onReloadClicked()
{
    if (m_currentTaskFile.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("没有已加载的任务"));
        return;
    }

    if (!m_drillManager) {
        return;
    }

    if (m_drillManager->loadTaskFile(m_currentTaskFile)) {
        updateStepsTable();
        updatePresetsTable();
        appendLog(tr("任务已重新加载"));
    }
}

void AutoTaskPage::onImportTaskClicked()
{
    QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("导入任务文件"),
        QDir::homePath(),
        tr("JSON 文件 (*.json);;所有文件 (*.*)"));

    if (fileName.isEmpty()) {
        return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("错误"), tr("无法打开任务文件"));
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, tr("错误"),
            tr("任务文件不是有效的JSON对象: %1").arg(parseError.errorString()));
        return;
    }

    QJsonObject root = doc.object();
    if (!root.contains("steps") || !root.value("steps").isArray()) {
        QMessageBox::warning(this, tr("错误"),
            tr("任务文件缺少 steps 数组"));
        return;
    }

    // Copy file to tasks directory
    QFileInfo fileInfo(fileName);
    QDir tasksDir(m_tasksDirectory);
    if (!tasksDir.exists() && !tasksDir.mkpath(".")) {
        QMessageBox::warning(this, tr("错误"), tr("无法创建任务目录"));
        return;
    }
    QString destPath = tasksDir.filePath(fileInfo.fileName());

    if (QFile::exists(destPath)) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            tr("确认"),
            tr("文件 %1 已存在，是否覆盖？").arg(fileInfo.fileName()),
            QMessageBox::Yes | QMessageBox::No);

        if (reply != QMessageBox::Yes) {
            return;
        }

        QFile::remove(destPath);
    }

    if (QFile::copy(fileName, destPath)) {
        loadTasksFromDirectory();
        appendLog(tr("任务文件已导入: %1").arg(fileInfo.fileName()));
    } else {
        QMessageBox::warning(this, tr("错误"), tr("无法导入任务文件"));
    }
}

void AutoTaskPage::onStartClicked()
{
    if (!m_drillManager) {
        QMessageBox::warning(this, tr("错误"),
            tr("控制器未连接\n\n请先在「钻机高级控制」页面连接控制器。"));
        return;
    }

    // 采集准备检查
    if (!ensureAcquisitionReady()) {
        appendLog(tr("任务未启动：数据采集未就绪"));
        return;
    }

    // 检查传感器数据连接
    if (!m_drillManager->hasSensorData()) {
        QMessageBox::warning(this, tr("错误"),
            tr("传感器数据未连接\n\n"
               "自动任务需要实时监控扭矩、钻压、位置等传感器数据。\n"
               "请先在「数据采集」页面启动数据采集。"));
        return;
    }

    if (m_drillManager->steps().isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("请先加载任务"));
        return;
    }

    if (m_drillManager->start()) {
        m_taskElapsed.start();
        m_elapsedTimer->start();
    }
}

void AutoTaskPage::onPauseClicked()
{
    if (m_drillManager) {
        m_drillManager->pause();
        m_elapsedTimer->stop();
    }
}

void AutoTaskPage::onResumeClicked()
{
    if (m_drillManager) {
        if (m_drillManager->resume()) {
            m_elapsedTimer->start();
        }
    }
}

void AutoTaskPage::onStopClicked()
{
    if (!m_drillManager) {
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        tr("确认停止"),
        tr("确定要停止当前任务吗？"),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        m_drillManager->abort();
        m_elapsedTimer->stop();
    }
}

void AutoTaskPage::onEmergencyClicked()
{
    if (!m_drillManager) {
        return;
    }

    QMessageBox::critical(
        this,
        tr("急停"),
        tr("急停已触发！所有运动将立即停止。"));

    m_drillManager->emergencyStop();
    m_elapsedTimer->stop();
}

void AutoTaskPage::onTaskListItemClicked()
{
    QListWidgetItem* item = ui->list_tasks->currentItem();
    if (!item) {
        return;
    }

    QString fileName = item->text();
    QString filePath = QDir(m_tasksDirectory).filePath(fileName);

    // Show task info
    ui->lbl_task_info->setText(tr("任务: %1\n路径: %2")
                                    .arg(fileName)
                                    .arg(filePath));
}

void AutoTaskPage::onTaskListItemDoubleClicked()
{
    QListWidgetItem* item = ui->list_tasks->currentItem();
    if (!item || !m_drillManager) {
        return;
    }

    QString fileName = item->text();
    QString filePath = QDir(m_tasksDirectory).filePath(fileName);

    if (m_drillManager->loadTaskFile(filePath)) {
        m_currentTaskFile = filePath;
        updateStepsTable();
        updatePresetsTable();
        appendLog(tr("任务文件已加载: %1").arg(fileName));
    }
}

void AutoTaskPage::onTaskStateChanged(AutoTaskState state, const QString& message)
{
    ui->lbl_task_status->setText(tr("状态: %1").arg(m_drillManager->stateString()));
    appendLog(tr("[状态] %1").arg(message));
    updateUIState();
}

void AutoTaskPage::onStepStarted(int index, const TaskStep& step)
{
    ui->lbl_current_step->setText(tr("当前步骤: %1/%2 - %3")
                                      .arg(index + 1)
                                      .arg(m_drillManager->steps().size())
                                      .arg(formatStepType(step.type)));

    ui->lbl_target->setText(tr("%1 mm").arg(step.targetDepthMm, 0, 'f', 1));

    // Show current preset
    if (!step.presetId.isEmpty()) {
        const auto& presets = m_drillManager->presets();
        if (presets.contains(step.presetId)) {
            const DrillParameterPreset& preset = presets[step.presetId];
            ui->lbl_current_preset->setText(
                tr("当前预设: %1 (Vp:%2 RPM:%3 Fi:%4)")
                    .arg(preset.id)
                    .arg(preset.feedSpeedMmPerMin)
                    .arg(preset.rotationRpm)
                    .arg(preset.impactFrequencyHz));
        }
    }

    highlightCurrentStep(index);
    updateStepStatus(index, "▶");
}

void AutoTaskPage::onStepCompleted(int index)
{
    updateStepStatus(index, "✓");
}

void AutoTaskPage::onProgressUpdated(double depthMm, double percent)
{
    ui->lbl_depth->setText(tr("%1 mm").arg(depthMm, 0, 'f', 1));
    ui->progress_step->setValue(static_cast<int>(percent));
    ui->progress_total->setValue(static_cast<int>(percent));
}

void AutoTaskPage::onTaskCompleted()
{
    m_elapsedTimer->stop();
    ui->progress_step->setValue(100);
    ui->progress_total->setValue(100);
    appendLog(tr("[完成] 任务执行完成"));

    QMessageBox::information(this, tr("任务完成"), tr("自动钻进任务已完成！"));
}

void AutoTaskPage::onTaskFailed(const QString& reason)
{
    m_elapsedTimer->stop();
    appendLog(tr("[失败] %1").arg(reason));

    QMessageBox::critical(this, tr("任务失败"), reason);
}

void AutoTaskPage::onLogMessage(const QString& message)
{
    appendLog(message);
}

void AutoTaskPage::onElapsedTimerTick()
{
    qint64 elapsed = m_taskElapsed.elapsed();
    ui->lbl_elapsed->setText(formatElapsedTime(elapsed));
}

void AutoTaskPage::loadTasksFromDirectory()
{
    QDir dir(m_tasksDirectory);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    m_availableTasks = dir.entryList(QStringList() << "*.json", QDir::Files);
    updateTaskList();
}

void AutoTaskPage::updateTaskList()
{
    ui->list_tasks->clear();
    ui->list_tasks->addItems(m_availableTasks);
}

void AutoTaskPage::updateStepsTable()
{
    ui->table_steps->setRowCount(0);

    if (!m_drillManager) {
        return;
    }

    const QVector<TaskStep>& steps = m_drillManager->steps();
    ui->table_steps->setRowCount(steps.size());

    for (int i = 0; i < steps.size(); ++i) {
        const TaskStep& step = steps[i];

        // Column 0: #
        QTableWidgetItem* item0 = new QTableWidgetItem(QString::number(i + 1));
        item0->setTextAlignment(Qt::AlignCenter);
        ui->table_steps->setItem(i, 0, item0);

        // Column 1: Type
        QTableWidgetItem* item1 = new QTableWidgetItem(formatStepType(step.type));
        item1->setTextAlignment(Qt::AlignCenter);
        ui->table_steps->setItem(i, 1, item1);

        // Column 2: Target
        QTableWidgetItem* item2 = new QTableWidgetItem(formatStepTarget(step));
        item2->setTextAlignment(Qt::AlignCenter);
        ui->table_steps->setItem(i, 2, item2);

        // Column 3: Preset
        QTableWidgetItem* item3 = new QTableWidgetItem(step.presetId);
        item3->setTextAlignment(Qt::AlignCenter);
        ui->table_steps->setItem(i, 3, item3);

        // Column 4: Status
        QTableWidgetItem* item4 = new QTableWidgetItem("...");
        item4->setTextAlignment(Qt::AlignCenter);
        ui->table_steps->setItem(i, 4, item4);
    }
}

void AutoTaskPage::updatePresetsTable()
{
    ui->table_presets->setRowCount(0);

    if (!m_drillManager) {
        return;
    }

    const QMap<QString, DrillParameterPreset>& presets = m_drillManager->presets();
    ui->table_presets->setRowCount(presets.size());

    int row = 0;
    for (auto it = presets.constBegin(); it != presets.constEnd(); ++it) {
        const DrillParameterPreset& preset = it.value();

        // Column 0: ID
        QTableWidgetItem* item0 = new QTableWidgetItem(preset.id);
        item0->setTextAlignment(Qt::AlignCenter);
        ui->table_presets->setItem(row, 0, item0);

        // Column 1: Vp
        QTableWidgetItem* item1 = new QTableWidgetItem(
            QString::number(preset.feedSpeedMmPerMin, 'f', 1));
        item1->setTextAlignment(Qt::AlignCenter);
        ui->table_presets->setItem(row, 1, item1);

        // Column 2: RPM
        QTableWidgetItem* item2 = new QTableWidgetItem(
            QString::number(preset.rotationRpm, 'f', 0));
        item2->setTextAlignment(Qt::AlignCenter);
        ui->table_presets->setItem(row, 2, item2);

        // Column 3: Fi
        QTableWidgetItem* item3 = new QTableWidgetItem(
            QString::number(preset.impactFrequencyHz, 'f', 1));
        item3->setTextAlignment(Qt::AlignCenter);
        ui->table_presets->setItem(row, 3, item3);

        ++row;
    }
}

void AutoTaskPage::updateUIState()
{
    if (!m_drillManager) {
        return;
    }

    AutoTaskState state = m_drillManager->state();

    // Enable/disable buttons based on state
    ui->btn_start->setEnabled(state == AutoTaskState::Idle);
    ui->btn_pause->setEnabled(state == AutoTaskState::Moving ||
                               state == AutoTaskState::Drilling);
    ui->btn_resume->setEnabled(state == AutoTaskState::Paused);
    ui->btn_stop->setEnabled(state != AutoTaskState::Idle &&
                              state != AutoTaskState::Finished);
}

void AutoTaskPage::updateStepStatus(int stepIndex, const QString& status)
{
    if (stepIndex < 0 || stepIndex >= ui->table_steps->rowCount()) {
        return;
    }

    QTableWidgetItem* item = ui->table_steps->item(stepIndex, 4);
    if (item) {
        item->setText(status);

        // Color code status
        if (status == "✓") {
            item->setForeground(QBrush(QColor("#67c23a")));
        } else if (status == "▶") {
            item->setForeground(QBrush(QColor("#409eff")));
        } else if (status == "✗") {
            item->setForeground(QBrush(QColor("#f56c6c")));
        }
    }
}

void AutoTaskPage::highlightCurrentStep(int stepIndex)
{
    // Clear previous highlight
    for (int row = 0; row < ui->table_steps->rowCount(); ++row) {
        for (int col = 0; col < ui->table_steps->columnCount(); ++col) {
            QTableWidgetItem* item = ui->table_steps->item(row, col);
            if (item) {
                item->setBackground(QBrush(Qt::white));
            }
        }
    }

    // Highlight current step
    if (stepIndex >= 0 && stepIndex < ui->table_steps->rowCount()) {
        for (int col = 0; col < ui->table_steps->columnCount(); ++col) {
            QTableWidgetItem* item = ui->table_steps->item(stepIndex, col);
            if (item) {
                item->setBackground(QBrush(QColor("#ecf5ff")));
            }
        }
    }
}

void AutoTaskPage::appendLog(const QString& message)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString logEntry = QString("[%1] %2").arg(timestamp, message);
    ui->tb_log->append(logEntry);
}

QString AutoTaskPage::formatStepType(TaskStep::Type type) const
{
    switch (type) {
    case TaskStep::Type::Positioning:
        return tr("定位");
    case TaskStep::Type::Drilling:
        return tr("钻进");
    case TaskStep::Type::Hold:
        return tr("保持");
    default:
        return tr("未知");
    }
}

QString AutoTaskPage::formatStepTarget(const TaskStep& step) const
{
    if (step.type == TaskStep::Type::Hold) {
        return tr("%1秒").arg(step.holdTimeSec);
    } else {
        return tr("%1mm").arg(step.targetDepthMm, 0, 'f', 1);
    }
}

QString AutoTaskPage::formatElapsedTime(qint64 msec) const
{
    int seconds = static_cast<int>(msec / 1000);
    int minutes = seconds / 60;
    seconds = seconds % 60;

    return QString("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

// ==================================================
// AutoTask-Acquisition 集成方法
// ==================================================

bool AutoTaskPage::ensureAcquisitionReady()
{
    // 如果没有AcquisitionManager，跳过（向后兼容）
    if (!m_acquisitionManager) {
        return true;
    }

    bool isRunning = m_acquisitionManager->isRunning();
    int currentRound = m_acquisitionManager->currentRoundId();

    // 场景1：采集未运行
    if (!isRunning) {
        auto reply = QMessageBox::question(
            this,
            tr("启动数据采集?"),
            tr("当前未启用数据采集。\n\n"
               "是否启动采集并创建新的实验轮次？\n"
               "（备注将标记为：%1）").arg(formatTaskNote()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes
        );

        if (reply == QMessageBox::No) {
            return false;  // 用户拒绝，中止任务启动
        }

        // 先创建轮次
        QString note = formatTaskNote();
        m_acquisitionManager->startNewRound("AutoTask", note);

        // 验证轮次是否成功创建
        int newRoundId = m_acquisitionManager->currentRoundId();
        if (newRoundId <= 0) {
            QMessageBox::critical(this, tr("错误"), tr("创建实验轮次失败"));
            return false;
        }

        // 轮次创建成功后才启动采集
        m_acquisitionManager->startAll();

        // 验证采集是否成功启动
        if (!m_acquisitionManager->isRunning()) {
            QMessageBox::critical(this, tr("错误"), tr("启动数据采集失败"));
            m_acquisitionManager->endCurrentRound();  // 清理已创建的轮次
            return false;
        }

        appendLog(tr("[数据采集] 已启动，轮次ID: %1").arg(newRoundId));
        return true;
    }

    // 场景2：采集运行中，但没有活动轮次
    if (currentRound == 0) {
        QString note = formatTaskNote();
        m_acquisitionManager->startNewRound("AutoTask", note);

        // 验证轮次是否成功创建
        int newRoundId = m_acquisitionManager->currentRoundId();
        if (newRoundId <= 0) {
            QMessageBox::critical(this, tr("错误"), tr("创建实验轮次失败"));
            return false;
        }

        appendLog(tr("[数据采集] 已创建轮次: %1").arg(newRoundId));
        return true;
    }

    // 场景3：采集运行中且有活动轮次 - 询问是否创建新轮次
    auto reply = QMessageBox::question(
        this,
        tr("创建新轮次?"),
        tr("当前已有活动的实验轮次 (ID: %1)。\n\n"
           "是否为本次任务创建新的轮次？").arg(currentRound),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No  // 默认：继续使用现有轮次
    );

    if (reply == QMessageBox::Yes) {
        QString note = formatTaskNote();
        m_acquisitionManager->startNewRound("AutoTask", note);

        // 验证轮次是否成功创建
        int newRoundId = m_acquisitionManager->currentRoundId();
        if (newRoundId <= 0 || newRoundId == currentRound) {
            QMessageBox::warning(this, tr("警告"), tr("创建新轮次失败，将继续使用现有轮次"));
        } else {
            appendLog(tr("[数据采集] 已创建新轮次: %1").arg(newRoundId));
        }
    }

    return true;
}

QString AutoTaskPage::formatTaskNote() const
{
    if (m_currentTaskFile.isEmpty()) {
        return tr("AutoTask");
    }
    QFileInfo info(m_currentTaskFile);
    return tr("AutoTask:%1").arg(info.fileName());
}

void AutoTaskPage::logAcquisitionEvent(bool running)
{
    if (running) {
        appendLog(tr("[数据采集] 已启动"));
    } else {
        appendLog(tr("[数据采集] 已停止"));
    }
}

// ==================================================
// 测试功能实现（仅在测试模式下编译）
// ==================================================
#ifdef ENABLE_TEST_MODE

void AutoTaskPage::setupTestUI()
{
    // 创建测试控制面板
    QGroupBox* testGroup = new QGroupBox(tr("🧪 测试功能（开发模式）"), this);
    testGroup->setStyleSheet("QGroupBox { font-weight: bold; color: #FF6600; }");

    QVBoxLayout* testLayout = new QVBoxLayout(testGroup);

    // 单元测试按钮
    QPushButton* btnUnitTest = new QPushButton(tr("运行单元测试"), this);
    btnUnitTest->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;");
    connect(btnUnitTest, &QPushButton::clicked,
            this, &AutoTaskPage::onRunUnitTestsClicked);
    testLayout->addWidget(btnUnitTest);

    // 分隔线
    QLabel* separator1 = new QLabel(tr("--- 模拟数据场景 ---"), this);
    separator1->setAlignment(Qt::AlignCenter);
    testLayout->addWidget(separator1);

    // 场景测试按钮组
    QHBoxLayout* scenarioRow1 = new QHBoxLayout();
    QPushButton* btnNormal = new QPushButton(tr("正常钻进"), this);
    QPushButton* btnTorque = new QPushButton(tr("扭矩超限"), this);
    connect(btnNormal, &QPushButton::clicked,
            this, &AutoTaskPage::onTestScenarioNormalClicked);
    connect(btnTorque, &QPushButton::clicked,
            this, &AutoTaskPage::onTestScenarioTorqueClicked);
    scenarioRow1->addWidget(btnNormal);
    scenarioRow1->addWidget(btnTorque);
    testLayout->addLayout(scenarioRow1);

    QHBoxLayout* scenarioRow2 = new QHBoxLayout();
    QPushButton* btnPressure = new QPushButton(tr("钻压超限"), this);
    QPushButton* btnStall = new QPushButton(tr("堵转"), this);
    connect(btnPressure, &QPushButton::clicked,
            this, &AutoTaskPage::onTestScenarioPressureClicked);
    connect(btnStall, &QPushButton::clicked,
            this, &AutoTaskPage::onTestScenarioStallClicked);
    scenarioRow2->addWidget(btnPressure);
    scenarioRow2->addWidget(btnStall);
    testLayout->addLayout(scenarioRow2);

    QPushButton* btnProgressive = new QPushButton(tr("逐步恶化"), this);
    connect(btnProgressive, &QPushButton::clicked,
            this, &AutoTaskPage::onTestScenarioProgressiveClicked);
    testLayout->addWidget(btnProgressive);

    // 停止模拟按钮
    QPushButton* btnStopMock = new QPushButton(tr("停止模拟数据"), this);
    btnStopMock->setStyleSheet("background-color: #F44336; color: white;");
    connect(btnStopMock, &QPushButton::clicked,
            this, &AutoTaskPage::onStopMockDataClicked);
    testLayout->addWidget(btnStopMock);

    // 添加到主布局（假设主布局是QVBoxLayout）
    if (QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(this->layout())) {
        mainLayout->addWidget(testGroup);
    } else {
        // 如果主布局不是VBoxLayout，尝试找到合适的位置添加
        testGroup->setParent(this);
        testGroup->setGeometry(10, 10, 200, 300);
        testGroup->show();
    }

    // 创建MockDataGenerator
    m_mockGenerator = new MockDataGenerator(this);

    // 连接到AutoDrillManager（如果已创建）
    if (m_drillManager) {
        connect(m_mockGenerator, &MockDataGenerator::dataBlockReady,
                m_drillManager, &AutoDrillManager::onDataBlockReceived,
                Qt::UniqueConnection);
    }
    // 注意：如果 drillManager 还未创建，会在 setControllers() 中连接

    connect(m_mockGenerator, &MockDataGenerator::scenarioChanged,
            [this](const QString& desc) {
        appendLog(tr("[测试] %1").arg(desc));
    });

    appendLog(tr("[测试模式] 测试功能已启用"));
}

void AutoTaskPage::onRunUnitTestsClicked()
{
    appendLog(tr("开始运行单元测试..."));
    testAutoTask();
    appendLog(tr("单元测试完成，请查看调试输出窗口"));
}

void AutoTaskPage::onTestScenarioNormalClicked()
{
    if (!m_mockGenerator) return;

    m_mockGenerator->setScenario(MockDataGenerator::SimulationScenario::NormalDrilling);
    m_mockGenerator->setUpdateInterval(100);  // 10Hz
    m_mockGenerator->startSimulation();

    appendLog(tr("[测试] 开始模拟：正常钻进场景"));
}

void AutoTaskPage::onTestScenarioTorqueClicked()
{
    if (!m_mockGenerator) return;

    m_mockGenerator->setScenario(MockDataGenerator::SimulationScenario::TorqueOverload);
    m_mockGenerator->setUpdateInterval(100);
    m_mockGenerator->startSimulation();

    appendLog(tr("[测试] 开始模拟：扭矩超限场景（30帧后触发）"));
}

void AutoTaskPage::onTestScenarioPressureClicked()
{
    if (!m_mockGenerator) return;

    m_mockGenerator->setScenario(MockDataGenerator::SimulationScenario::PressureOverload);
    m_mockGenerator->setUpdateInterval(100);
    m_mockGenerator->startSimulation();

    appendLog(tr("[测试] 开始模拟：钻压超限场景（20帧后触发）"));
}

void AutoTaskPage::onTestScenarioStallClicked()
{
    if (!m_mockGenerator) return;

    m_mockGenerator->setScenario(MockDataGenerator::SimulationScenario::Stall);
    m_mockGenerator->setUpdateInterval(100);
    m_mockGenerator->startSimulation();

    appendLog(tr("[测试] 开始模拟：堵转场景（1秒后触发）"));
}

void AutoTaskPage::onTestScenarioProgressiveClicked()
{
    if (!m_mockGenerator) return;

    m_mockGenerator->setScenario(MockDataGenerator::SimulationScenario::ProgressiveFailure);
    m_mockGenerator->setUpdateInterval(100);
    m_mockGenerator->startSimulation();

    appendLog(tr("[测试] 开始模拟：逐步恶化场景（正常→异常→故障）"));
}

void AutoTaskPage::onStopMockDataClicked()
{
    if (!m_mockGenerator) return;

    m_mockGenerator->stopSimulation();
    appendLog(tr("[测试] 模拟数据已停止"));
}

#endif  // ENABLE_TEST_MODE

// ==================================================
// 测试功能存根（禁用测试模式时防止链接错误）
// ==================================================
#ifndef ENABLE_TEST_MODE
// 当测试模式禁用时，moc 可能仍然引用这些槽函数
// 提供空实现以避免链接错误
void AutoTaskPage::onRunUnitTestsClicked() {}
void AutoTaskPage::onTestScenarioNormalClicked() {}
void AutoTaskPage::onTestScenarioTorqueClicked() {}
void AutoTaskPage::onTestScenarioPressureClicked() {}
void AutoTaskPage::onTestScenarioStallClicked() {}
void AutoTaskPage::onTestScenarioProgressiveClicked() {}
void AutoTaskPage::onStopMockDataClicked() {}
#endif  // !ENABLE_TEST_MODE
