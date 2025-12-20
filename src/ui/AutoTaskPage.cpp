#include "ui/AutoTaskPage.h"
#include "ui_AutoTaskPage.h"

#include "control/FeedController.h"
#include "control/RotationController.h"
#include "control/PercussionController.h"
#include "control/AcquisitionManager.h"
#include "database/DbWriter.h"
#include "dataACQ/MdbWorker.h"
#include "dataACQ/MotorWorker.h"

#include <QFileDialog>
#include <QDir>
#include <QDateTime>
#include <QTimer>
#include <QMessageBox>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSignalBlocker>
#include <QStringList>
#include <QtGlobal>
#include <QFileInfo>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QVariant>
#include <cmath>

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
    , m_eventDbConnectionName(QString("AutoTaskPage_%1").arg(reinterpret_cast<quintptr>(this)))
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
    setupRecordPanel();
    reloadFilters();
    reloadExecutionRecords();

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

    if (m_eventDb.isValid() && m_eventDb.isOpen()) {
        m_eventDb.close();
    }
    m_eventDb = QSqlDatabase();
    if (!m_eventDbConnectionName.isEmpty()) {
        QSqlDatabase::removeDatabase(m_eventDbConnectionName);
    }

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
            m_drillManager->setDataWorkers(m_acquisitionManager->mdbWorker(),
                                           m_acquisitionManager->motorWorker());
            m_drillManager->setDbWriter(m_acquisitionManager->dbWriter());
            m_drillManager->setRoundId(m_acquisitionManager->currentRoundId());
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
        m_drillManager->setDbWriter(manager->dbWriter());
        m_drillManager->setRoundId(manager->currentRoundId());
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

    // Execution record panel
    connect(ui->btn_refresh_records, &QPushButton::clicked,
            this, &AutoTaskPage::onRefreshRecordsClicked);
    connect(ui->combo_round_filter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AutoTaskPage::onRoundFilterChanged);
    connect(ui->combo_task_filter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AutoTaskPage::onTaskFilterChanged);
    connect(ui->table_execution_records, &QTableWidget::itemSelectionChanged,
            this, &AutoTaskPage::onRecordSelectionChanged);
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

    // Copy file to tasks directory
    QFileInfo fileInfo(fileName);
    QString destPath = QDir(m_tasksDirectory).filePath(fileInfo.fileName());

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

    syncRoundContext();

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
        syncRoundContext();
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
    Q_UNUSED(state);
    ui->lbl_task_status->setText(tr("状态: %1").arg(m_drillManager->stateString()));
    appendLog(tr("[状态] %1").arg(message));
    updateUIState();
    reloadRecordsAsync();
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
    reloadRecordsAsync();
}

void AutoTaskPage::onStepCompleted(int index)
{
    updateStepStatus(index, "✓");
    reloadRecordsAsync();
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
    reloadRecordsAsync();
}

void AutoTaskPage::onTaskFailed(const QString& reason)
{
    m_elapsedTimer->stop();
    appendLog(tr("[失败] %1").arg(reason));

    QMessageBox::critical(this, tr("任务失败"), reason);
    reloadRecordsAsync();
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

void AutoTaskPage::syncRoundContext()
{
    if (!m_drillManager || !m_acquisitionManager) {
        return;
    }

    m_drillManager->setRoundId(m_acquisitionManager->currentRoundId());
    m_drillManager->setDbWriter(m_acquisitionManager->dbWriter());
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
        syncRoundContext();
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
        syncRoundContext();
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

    syncRoundContext();
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

void AutoTaskPage::onRefreshRecordsClicked()
{
    reloadFilters();
    reloadExecutionRecords();
}

void AutoTaskPage::onRoundFilterChanged(int)
{
    reloadFilters();
    reloadExecutionRecords();
}

void AutoTaskPage::onTaskFilterChanged(int)
{
    reloadExecutionRecords();
}

void AutoTaskPage::onRecordSelectionChanged()
{
    showRecordDetails(ui->table_execution_records->currentRow());
}

void AutoTaskPage::setupRecordPanel()
{
    ui->table_execution_records->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->table_execution_records->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->table_execution_records->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->table_execution_records->setAlternatingRowColors(true);
    ui->table_execution_records->horizontalHeader()->setStretchLastSection(true);
    ui->table_execution_records->setColumnWidth(0, 140); // 时间
    ui->table_execution_records->setColumnWidth(1, 90);  // 状态
    ui->table_execution_records->setColumnWidth(2, 70);  // 步骤
    ui->table_execution_records->setColumnWidth(3, 90);  // 深度
    ui->table_execution_records->setColumnWidth(4, 90);  // 扭矩
    ui->table_execution_records->setColumnWidth(5, 90);  // 钻压
}

bool AutoTaskPage::ensureEventDatabase()
{
    if (m_eventDb.isValid() && m_eventDb.isOpen()) {
        return true;
    }

    if (!m_eventDb.isValid()) {
        m_eventDb = QSqlDatabase::addDatabase("QSQLITE", m_eventDbConnectionName);
        m_eventDb.setDatabaseName("database/drill_data.db");
    }

    if (!m_eventDb.isOpen() && !m_eventDb.open()) {
        appendLog(tr("无法打开执行记录数据库: %1").arg(m_eventDb.lastError().text()));
        return false;
    }

    return true;
}

void AutoTaskPage::reloadFilters()
{
    if (!ensureEventDatabase()) {
        return;
    }

    int currentRound = ui->combo_round_filter->currentData().toInt();
    QString currentTask = ui->combo_task_filter->currentData().toString();

    QSignalBlocker roundBlocker(ui->combo_round_filter);
    ui->combo_round_filter->clear();
    ui->combo_round_filter->addItem(tr("全部轮次"), 0);

    QSqlQuery roundQuery(m_eventDb);
    if (roundQuery.exec("SELECT DISTINCT round_id FROM auto_task_events ORDER BY round_id DESC")) {
        while (roundQuery.next()) {
            int roundId = roundQuery.value(0).toInt();
            ui->combo_round_filter->addItem(QString::number(roundId), roundId);
        }
    } else {
        appendLog(tr("读取轮次过滤器失败: %1").arg(roundQuery.lastError().text()));
    }

    int roundIndex = ui->combo_round_filter->findData(currentRound);
    if (roundIndex >= 0) {
        ui->combo_round_filter->setCurrentIndex(roundIndex);
    }

    int selectedRound = ui->combo_round_filter->currentData().toInt();

    QSignalBlocker taskBlocker(ui->combo_task_filter);
    ui->combo_task_filter->clear();
    ui->combo_task_filter->addItem(tr("全部任务"), QString());

    QString taskSql = "SELECT DISTINCT task_file FROM auto_task_events";
    if (selectedRound > 0) {
        taskSql += " WHERE round_id = :round_id";
    }
    taskSql += " ORDER BY task_file";

    QSqlQuery taskQuery(m_eventDb);
    taskQuery.prepare(taskSql);
    if (selectedRound > 0) {
        taskQuery.bindValue(":round_id", selectedRound);
    }

    if (taskQuery.exec()) {
        while (taskQuery.next()) {
            QString taskFile = taskQuery.value(0).toString();
            QString displayName = taskFile.isEmpty()
                ? tr("<未命名任务>")
                : QFileInfo(taskFile).fileName();
            ui->combo_task_filter->addItem(displayName, taskFile);
        }
    } else {
        appendLog(tr("读取任务过滤器失败: %1").arg(taskQuery.lastError().text()));
    }

    int taskIndex = ui->combo_task_filter->findData(currentTask);
    if (taskIndex >= 0) {
        ui->combo_task_filter->setCurrentIndex(taskIndex);
    }
}

void AutoTaskPage::reloadRecordsAsync()
{
    QTimer::singleShot(150, this, [this]() {
        reloadFilters();
        reloadExecutionRecords();
    });
}

void AutoTaskPage::reloadExecutionRecords()
{
    if (!ensureEventDatabase()) {
        return;
    }

    m_executionRecords.clear();

    int roundFilter = ui->combo_round_filter->currentData().toInt();
    QString taskFilter = ui->combo_task_filter->currentData().toString();

    QStringList conditions;
    if (roundFilter > 0) {
        conditions << "round_id = :round_id";
    }
    if (!taskFilter.isEmpty()) {
        conditions << "task_file = :task_file";
    }

    QString sql = "SELECT event_id, round_id, task_file, step_index, state, reason, "
                  "depth_mm, torque_nm, pressure_n, velocity_mm_per_min, "
                  "force_upper_n, force_lower_n, timestamp_us "
                  "FROM auto_task_events";
    if (!conditions.isEmpty()) {
        sql += " WHERE " + conditions.join(" AND ");
    }
    sql += " ORDER BY timestamp_us DESC";

    QSqlQuery query(m_eventDb);
    query.prepare(sql);
    if (roundFilter > 0) {
        query.bindValue(":round_id", roundFilter);
    }
    if (!taskFilter.isEmpty()) {
        query.bindValue(":task_file", taskFilter);
    }

    if (!query.exec()) {
        appendLog(tr("读取执行记录失败: %1").arg(query.lastError().text()));
        return;
    }

    auto readDouble = [](const QVariant& value) {
        return value.isNull() ? qQNaN() : value.toDouble();
    };

    while (query.next()) {
        ExecutionRecord record;
        record.eventId = query.value(0).toInt();
        record.roundId = query.value(1).toInt();
        record.taskFile = query.value(2).toString();
        record.stepIndex = query.value(3).isNull() ? -1 : query.value(3).toInt();
        record.state = query.value(4).toString();
        record.reason = query.value(5).toString();
        record.depthMm = readDouble(query.value(6));
        record.torqueNm = readDouble(query.value(7));
        record.pressureN = readDouble(query.value(8));
        record.velocityMmPerMin = readDouble(query.value(9));
        record.forceUpperN = readDouble(query.value(10));
        record.forceLowerN = readDouble(query.value(11));
        record.timestampUs = query.value(12).toLongLong();
        m_executionRecords.append(record);
    }

    updateRecordTable();
    showRecordDetails(!m_executionRecords.isEmpty() ? 0 : -1);
}

QString AutoTaskPage::formatRecordTimestamp(qint64 timestampUs) const
{
    if (timestampUs <= 0) {
        return "--";
    }
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(timestampUs / 1000);
    return dt.toString("MM-dd hh:mm:ss");
}

QString AutoTaskPage::formatSensorSnapshot(const ExecutionRecord& record) const
{
    auto valueOrPlaceholder = [](double value, const QString& unit) {
        if (std::isnan(value)) {
            return QStringLiteral("-- %1").arg(unit);
        }
        return QString("%1 %2").arg(QString::number(value, 'f', 1), unit);
    };

    QStringList parts;
    parts << tr("深度 %1").arg(valueOrPlaceholder(record.depthMm, "mm"));
    parts << tr("扭矩 %1").arg(valueOrPlaceholder(record.torqueNm, "Nm"));
    parts << tr("钻压 %1").arg(valueOrPlaceholder(record.pressureN, "N"));
    parts << tr("速度 %1").arg(valueOrPlaceholder(record.velocityMmPerMin, "mm/min"));
    parts << tr("上拉 %1").arg(valueOrPlaceholder(record.forceUpperN, "N"));
    parts << tr("下拉 %1").arg(valueOrPlaceholder(record.forceLowerN, "N"));
    return parts.join(" | ");
}

void AutoTaskPage::updateRecordTable()
{
    ui->table_execution_records->setRowCount(m_executionRecords.size());

    auto displayState = [this](const QString& state) -> QString {
        if (state == "started") return tr("开始");
        if (state == "resumed") return tr("恢复");
        if (state == "step_started") return tr("步骤开始");
        if (state == "step_completed") return tr("步骤完成");
        if (state == "finished") return tr("完成");
        if (state == "failed") return tr("失败");
        return state;
    };

    auto formatNumber = [](double value) {
        return std::isnan(value) ? QStringLiteral("--") : QString::number(value, 'f', 1);
    };

    for (int row = 0; row < m_executionRecords.size(); ++row) {
        const ExecutionRecord& record = m_executionRecords.at(row);

        ui->table_execution_records->setItem(row, 0,
            new QTableWidgetItem(formatRecordTimestamp(record.timestampUs)));
        ui->table_execution_records->setItem(row, 1,
            new QTableWidgetItem(displayState(record.state)));
        QString stepText = (record.stepIndex >= 0)
            ? QString::number(record.stepIndex + 1)
            : QStringLiteral("--");
        ui->table_execution_records->setItem(row, 2, new QTableWidgetItem(stepText));
        ui->table_execution_records->setItem(row, 3,
            new QTableWidgetItem(formatNumber(record.depthMm)));
        ui->table_execution_records->setItem(row, 4,
            new QTableWidgetItem(formatNumber(record.torqueNm)));
        ui->table_execution_records->setItem(row, 5,
            new QTableWidgetItem(formatNumber(record.pressureN)));
        ui->table_execution_records->setItem(row, 6,
            new QTableWidgetItem(record.reason));
    }

    ui->table_execution_records->resizeRowsToContents();

    if (!m_executionRecords.isEmpty()) {
        ui->table_execution_records->setCurrentCell(0, 0);
    } else {
        ui->table_execution_records->clearSelection();
    }
}

void AutoTaskPage::showRecordDetails(int row)
{
    if (row < 0 || row >= m_executionRecords.size()) {
        ui->lbl_record_summary->setText(tr("未选择记录"));
        ui->lbl_sensor_snapshot->setText(tr("传感器摘要将显示在此处"));
        return;
    }

    const ExecutionRecord& record = m_executionRecords.at(row);
    QString displayState = ui->table_execution_records->item(row, 1)
        ? ui->table_execution_records->item(row, 1)->text()
        : record.state;
    QString taskLabel = record.taskFile.isEmpty()
        ? tr("<未命名任务>")
        : QFileInfo(record.taskFile).fileName();
    QString stepText = (record.stepIndex >= 0)
        ? QString::number(record.stepIndex + 1)
        : QStringLiteral("--");

    ui->lbl_record_summary->setText(
        tr("轮次 %1 | 任务: %2 | 状态: %3 | 步骤: %4 | 时间: %5")
            .arg(record.roundId)
            .arg(taskLabel)
            .arg(displayState)
            .arg(stepText)
            .arg(formatRecordTimestamp(record.timestampUs)));

    ui->lbl_sensor_snapshot->setText(formatSensorSnapshot(record));

    if (record.stepIndex >= 0) {
        highlightCurrentStep(record.stepIndex);
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
