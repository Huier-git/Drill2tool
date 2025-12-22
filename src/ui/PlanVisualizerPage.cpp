#include "ui/PlanVisualizerPage.h"
#include "ui_PlanVisualizerPage.h"
#include "qcustomplot.h"
#include "control/MotionConfigManager.h"
#include "control/MechanismDefs.h"

#include <QVBoxLayout>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QRegularExpression>
#include <QDebug>
#include <QHeaderView>
#include <QCoreApplication>
#include <QStringList>
#include <cmath>

// 获取项目根目录（从build/debug向上两级）
static QString getProjectRoot()
{
    QString appDir = QCoreApplication::applicationDirPath();
    return QDir::cleanPath(appDir + "/../..");
}

// DOF列表和颜色映射
static const QStringList DOFS = {"Fz", "Sr", "Me", "Mg", "Mr", "Dh", "Pr", "Pi", "Cb"};
static const QMap<QString, QColor> DOF_COLORS = {
    {"Fz", QColor(52, 152, 219)},   // 蓝色 - 进给
    {"Sr", QColor(155, 89, 182)},   // 紫色 - 存储机构
    {"Me", QColor(46, 204, 113)},   // 绿色 - 机械手伸缩
    {"Mg", QColor(241, 196, 15)},   // 黄色 - 机械手夹紧
    {"Mr", QColor(230, 126, 34)},   // 橙色 - 机械手回转
    {"Dh", QColor(231, 76, 60)},    // 红色 - 对接推杆
    {"Pr", QColor(26, 188, 156)},   // 青色 - 回转
    {"Pi", QColor(52, 73, 94)},     // 深灰蓝 - 冲击
    {"Cb", QColor(149, 165, 166)}   // 灰色 - 下夹紧
};

// 默认时长配置
static const QMap<QString, int> DEFAULT_DURATIONS = {
    // Stage A
    {"A_FZ_AH", 8}, {"A_ME_to_store", 3}, {"A_MG_grip", 3}, {"A_ME_back", 3},
    {"A_MR_to_head", 3}, {"A_ME_to_head", 3}, {"A_FZ_HG", 5},
    {"A_COUPLE_GE", 6}, {"A_DH_lock", 1}, {"A_MG_release", 2},
    {"A_ME_back_from_head", 3}, {"A_MR_back_to_store", 3},
    {"A_DRILL", 10}, {"A_CB_clamp", 5}, {"A_DH_unlock", 1},
    {"A_BREAK_AC", 6}, {"A_FZ_CH", 7},
    // Stage B
    {"SR_INDEX", 3},
    {"B_ME_to_store", 3}, {"B_MG_grip", 3}, {"B_ME_back", 3},
    {"B_MR_to_head", 3}, {"B_ME_to_head", 3},
    {"B_FZ_HF", 4}, {"B_COUPLE_FD", 6},
    {"B_DH_lock", 1}, {"B_MG_release", 2},
    {"B_ME_back_from_head", 3}, {"B_MR_back_to_store", 3},
    {"B_FZ_DJ", 4}, {"B_COUPLE_JI", 6},
    {"B_CB_release", 5}, {"B_DRILL", 10}, {"B_CB_clamp", 5},
    {"B_DH_unlock", 1}, {"B_BREAK_AC", 6}, {"B_FZ_CH", 7},
    // Stage C
    {"C_FZ_HC", 6}, {"C_COUPLE_CB", 6}, {"C_DH_lock", 1},
    {"C_CB_release", 5}, {"C_FZ_BI", 8}, {"C_CB_clamp", 5},
    {"C_BREAK_IJ", 6}, {"C_FZ_JD", 5},
    {"C_MR_Assist", 3}, {"C_ME_Assist", 3}, {"C_MG_Grip", 3},
    {"C_DH_unlock", 1}, {"C_BREAK_DF", 6}, {"C_FZ_FH", 7},
    {"C_ME_Retract", 3}, {"C_MR_Retract", 3},
    {"C_ME_Store", 3}, {"C_MG_Release", 2}, {"C_ME_Back", 3},
    {"C_SR_Next", 3},
    // Stage D
    {"D_FZ_HC", 6}, {"D_COUPLE_CB", 6}, {"D_DH_lock", 1},
    {"D_CB_release", 5}, {"D_FZ_BE", 6},
    {"D_MR_Assist", 3}, {"D_ME_Assist", 3}, {"D_MG_Grip", 3},
    {"D_DH_unlock", 1}, {"D_BREAK_EG", 6}, {"D_FZ_GH", 7},
    {"D_SR_Reset", 3},
    {"D_ME_Retract", 3}, {"D_MR_Retract", 3},
    {"D_ME_Store", 3}, {"D_MG_Release", 2}, {"D_ME_Back", 3}
};

namespace {
enum class StepKind {
    Move,
    StepIndex,
    Spin,
    Hold
};

struct StepMapping {
    QString axis;
    QString start;
    QString end;
    StepKind kind = StepKind::Move;

    bool valid() const
    {
        if (axis.isEmpty()) {
            return false;
        }
        if (kind == StepKind::Move) {
            return !start.isEmpty() && !end.isEmpty();
        }
        return true;
    }
};

using StepMappingList = QList<StepMapping>;
using StepMappingMap = QMap<QString, StepMappingList>;

QString normalizeAxisCode(const QString& code)
{
    QString upper = code.trimmed().toUpper();
    if (upper.size() == 2) {
        return QString("%1%2").arg(upper.left(1)).arg(upper.mid(1).toLower());
    }
    return upper;
}

StepMappingMap loadStepMappings(const QString& path, QStringList* warnings)
{
    StepMappingMap map;
    QFileInfo info(path);
    if (!info.exists()) {
        return map;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (warnings) {
            warnings->append(QString("Failed to open step map CSV: %1").arg(path));
        }
        return map;
    }

    QTextStream in(&file);
    int lineNo = 0;
    int keyIdx = 0;
    int axisIdx = 1;
    int startIdx = 2;
    int endIdx = 3;
    int kindIdx = 4;
    bool hasHeader = false;

    while (!in.atEnd()) {
        QString line = in.readLine();
        ++lineNo;
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) {
            continue;
        }

        QStringList cols = trimmed.split(',', Qt::KeepEmptyParts);
        if (!hasHeader) {
            QString first = cols.value(0).trimmed().toLower();
            if (first == "step_key" || first.contains("axis") || first.contains("start")) {
                for (int i = 0; i < cols.size(); ++i) {
                    QString key = cols[i].trimmed().toLower();
                    if (key == "step_key") keyIdx = i;
                    else if (key == "axis_code" || key == "axis") axisIdx = i;
                    else if (key == "start_key" || key == "start") startIdx = i;
                    else if (key == "end_key" || key == "end") endIdx = i;
                    else if (key == "kind") kindIdx = i;
                }
                hasHeader = true;
                continue;
            }
        }
        hasHeader = true;

        QString stepKey = cols.value(keyIdx).trimmed();
        QString axisCode = normalizeAxisCode(cols.value(axisIdx));
        QString startKey = cols.value(startIdx).trimmed().toUpper();
        QString endKey = cols.value(endIdx).trimmed().toUpper();
        QString kindText = cols.value(kindIdx).trimmed().toLower();

        if (stepKey.isEmpty() || axisCode.isEmpty()) {
            continue;
        }

        StepMapping mapping;
        mapping.axis = axisCode;
        mapping.start = startKey;
        mapping.end = endKey;
        if (kindText == "index" || kindText == "step_index") {
            mapping.kind = StepKind::StepIndex;
        } else if (kindText == "spin") {
            mapping.kind = StepKind::Spin;
        } else if (kindText == "hold") {
            mapping.kind = StepKind::Hold;
        }
        map[stepKey].append(mapping);
    }

    return map;
}

StepMappingList resolveMappings(const QString& key, const StepMappingMap& map)
{
    auto it = map.find(key);
    if (it != map.end()) {
        return it.value();
    }

    QRegularExpression re("^\\w_([A-Z]{2})_([A-J])([A-J])$",
                          QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(key);
    if (match.hasMatch()) {
        StepMapping mapping;
        mapping.axis = normalizeAxisCode(match.captured(1));
        mapping.start = match.captured(2).toUpper();
        mapping.end = match.captured(3).toUpper();
        return StepMappingList{mapping};
    }

    return StepMappingList();
}

double computeMoveTimeSec(double distance, double speed, double accel)
{
    if (distance <= 0.0 || speed <= 0.0) {
        return 0.0;
    }
    if (accel <= 0.0) {
        return distance / speed;
    }

    const double tAccel = speed / accel;
    const double distAccel = 0.5 * accel * tAccel * tAccel;
    if (2.0 * distAccel >= distance) {
        return 2.0 * std::sqrt(distance / accel);
    }

    const double distCruise = distance - 2.0 * distAccel;
    return 2.0 * tAccel + distCruise / speed;
}
} // namespace

PlanVisualizerPage::PlanVisualizerPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PlanVisualizerPage)
    , m_ganttChart(nullptr)
    , m_process(nullptr)
    , m_serialTime(0)
    , m_optimizedTime(0)
    , m_isOptimizedMode(false)
    , m_durationsModified(false)
{
    ui->setupUi(this);

    loadDefaultDurations();
    setupDurTable();
    setupGanttChart();
    setupConnections();
}

PlanVisualizerPage::~PlanVisualizerPage()
{
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    delete ui;
}

void PlanVisualizerPage::loadDefaultDurations()
{
    m_durations = DEFAULT_DURATIONS;
    QString configPath = getDurConfigPath();
    if (QFileInfo::exists(configPath)) {
        if (!loadDurationsFromJson(configPath, false)) {
            qWarning() << "Failed to load durations from:" << configPath;
        }
    }
}

void PlanVisualizerPage::setupDurTable()
{
    ui->durTable->setColumnCount(2);
    ui->durTable->setHorizontalHeaderLabels({"操作名称", "时长(s)"});
    ui->durTable->horizontalHeader()->setStretchLastSection(true);
    ui->durTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->durTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    ui->durTable->setColumnWidth(1, 80);
    populateDurTable();
}

void PlanVisualizerPage::populateDurTable()
{
    disconnect(ui->durTable, &QTableWidget::cellChanged, this, &PlanVisualizerPage::onDurTableCellChanged);
    ui->durTable->setRowCount(m_durations.size());

    int row = 0;
    for (auto it = m_durations.constBegin(); it != m_durations.constEnd(); ++it, ++row) {
        QTableWidgetItem* nameItem = new QTableWidgetItem(it.key());
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        ui->durTable->setItem(row, 0, nameItem);

        QTableWidgetItem* durItem = new QTableWidgetItem(QString::number(it.value()));
        ui->durTable->setItem(row, 1, durItem);
    }

    connect(ui->durTable, &QTableWidget::cellChanged, this, &PlanVisualizerPage::onDurTableCellChanged);
}

void PlanVisualizerPage::setupGanttChart()
{
    m_ganttChart = new QCustomPlot(this);

    // 替换ScrollArea内的占位符
    QLayout* scrollLayout = ui->ganttScrollContent->layout();
    scrollLayout->replaceWidget(ui->ganttPlaceholder, m_ganttChart);
    ui->ganttPlaceholder->hide();

    // 设置固定高度确保垂直空间充足
    m_ganttChart->setMinimumHeight(350);

    // 配置图表 - 仅允许水平拖动和缩放
    m_ganttChart->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_ganttChart->axisRect()->setRangeDrag(Qt::Horizontal);
    m_ganttChart->axisRect()->setRangeZoom(Qt::Horizontal);

    // Y轴：DOF名称（固定范围）
    QSharedPointer<QCPAxisTickerText> textTicker(new QCPAxisTickerText);
    for (int i = 0; i < DOFS.size(); ++i) {
        textTicker->addTick(i, DOFS[i]);
    }
    m_ganttChart->yAxis->setTicker(textTicker);
    m_ganttChart->yAxis->setRange(-0.5, DOFS.size() - 0.5);
    m_ganttChart->yAxis->setLabel("机构 (DOF)");

    // X轴：时间
    m_ganttChart->xAxis->setLabel("时间 (秒)");
    m_ganttChart->xAxis->setRange(0, 100);

    // 背景
    m_ganttChart->setBackground(QBrush(QColor(250, 250, 250)));
    m_ganttChart->axisRect()->setBackground(QBrush(Qt::white));

    // 连接水平滚动条与图表X轴
    connect(ui->ganttHScrollBar, &QScrollBar::valueChanged, this, [this](int value) {
        double range = m_ganttChart->xAxis->range().size();
        double newLower = value;
        m_ganttChart->xAxis->setRange(newLower, newLower + range);
        m_ganttChart->replot();
    });

    // 当图表X轴范围变化时更新滚动条
    connect(m_ganttChart->xAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged),
            this, [this](const QCPRange &newRange) {
        // 更新滚动条范围（阻止信号循环）
        ui->ganttHScrollBar->blockSignals(true);
        ui->ganttHScrollBar->setValue(static_cast<int>(newRange.lower));
        ui->ganttHScrollBar->blockSignals(false);
    });

    m_ganttChart->replot();
}

void PlanVisualizerPage::setupConnections()
{
    connect(ui->runButton, &QPushButton::clicked, this, &PlanVisualizerPage::onRunPlan);
    connect(ui->replanButton, &QPushButton::clicked, this, &PlanVisualizerPage::onReplan);
    connect(ui->editDurButton, &QPushButton::clicked, this, &PlanVisualizerPage::onEditDurations);
    connect(ui->exportJsonButton, &QPushButton::clicked, this, &PlanVisualizerPage::onExportJson);

    connect(ui->loadDurButton, &QPushButton::clicked, this, &PlanVisualizerPage::onLoadDurConfig);
    connect(ui->saveDurButton, &QPushButton::clicked, this, &PlanVisualizerPage::onSaveDurConfig);
    connect(ui->resetDurButton, &QPushButton::clicked, this, &PlanVisualizerPage::onResetDurConfig);
    connect(ui->autoDurButton, &QPushButton::clicked, this, &PlanVisualizerPage::onAutoComputeDurations);
}

void PlanVisualizerPage::onRunPlan()
{
    m_isOptimizedMode = ui->optimizedRadio->isChecked();
    int nPipes = ui->pipesSpinBox->value();
    int zoom = ui->zoomSpinBox->value();

    QString scriptName = m_isOptimizedMode ? "scheduler.py" : "serial.py";

    ui->statusLabel->setText(QString("正在运行 %1...").arg(scriptName));
    ui->runButton->setEnabled(false);
    ui->replanButton->setEnabled(false);
    ui->asciiOutput->clear();

    // 每次运行都写入时长配置，确保Python脚本使用最新参数
    writeDurationsToTempFile();

    runPythonScript(scriptName, nPipes, zoom);
}

void PlanVisualizerPage::onReplan()
{
    // 清空之前的结果
    m_currentAscii.clear();
    m_currentJson = QJsonObject();
    ui->asciiOutput->clear();
    ui->serialTimeLabel->setText("串行时间: --");
    ui->optimizedTimeLabel->setText("优化时间: --");
    ui->savedTimeLabel->setText("节省时间: --");

    // 重新运行规划（onRunPlan会自动写入最新配置）
    onRunPlan();
}

void PlanVisualizerPage::onEditDurations()
{
    bool visible = ui->durConfigGroup->isVisible();
    ui->durConfigGroup->setVisible(!visible);
    ui->editDurButton->setText(visible ? "编辑时长" : "隐藏时长");
}

void PlanVisualizerPage::onDurTableCellChanged(int row, int column)
{
    if (column != 1) return;

    QTableWidgetItem* nameItem = ui->durTable->item(row, 0);
    QTableWidgetItem* durItem = ui->durTable->item(row, 1);
    if (!nameItem || !durItem) return;

    QString name = nameItem->text();
    bool ok;
    int newDur = durItem->text().toInt(&ok);

    if (ok && newDur > 0) {
        m_durations[name] = newDur;
        m_durationsModified = true;
        ui->statusLabel->setText(QString("时长已修改: %1 = %2s").arg(name).arg(newDur));
    } else {
        durItem->setText(QString::number(m_durations.value(name, 1)));
        QMessageBox::warning(this, "输入错误", "请输入有效的正整数时长值");
    }
}

void PlanVisualizerPage::onLoadDurConfig()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        "加载时长配置", getDurConfigPath(), "JSON文件 (*.json)");
    if (fileName.isEmpty()) return;

    if (!loadDurationsFromJson(fileName, true)) {
        ui->statusLabel->setText("加载配置失败");
        return;
    }
    populateDurTable();
    m_durationsModified = true;
    ui->statusLabel->setText("已加载配置: " + QFileInfo(fileName).fileName());
}

void PlanVisualizerPage::onSaveDurConfig()
{
    QString fileName = QFileDialog::getSaveFileName(this,
        "保存时长配置", getDurConfigPath(), "JSON文件 (*.json)");
    if (fileName.isEmpty()) return;

    if (!saveDurationsToJson(fileName, true)) {
        ui->statusLabel->setText("保存配置失败");
        return;
    }
    ui->statusLabel->setText("已保存配置: " + QFileInfo(fileName).fileName());
}

void PlanVisualizerPage::onResetDurConfig()
{
    if (QMessageBox::question(this, "确认重置", "确定要重置为默认时长配置吗?",
            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        m_durations = DEFAULT_DURATIONS;
        populateDurTable();
        m_durationsModified = true;
        ui->statusLabel->setText("已重置为默认配置");
    }
}

void PlanVisualizerPage::onAutoComputeDurations()
{
    auto* configMgr = MotionConfigManager::instance();
    if (configMgr && configMgr->getAllConfigs().isEmpty()) {
        const QString cfgPath = getProjectRoot() + "/config/mechanisms.json";
        if (QFileInfo::exists(cfgPath)) {
            configMgr->loadConfig(cfgPath);
        }
    }

    const QMap<Mechanism::Code, MechanismParams> configs =
        configMgr ? configMgr->getAllConfigs() : QMap<Mechanism::Code, MechanismParams>();
    if (configs.isEmpty()) {
        QMessageBox::warning(this, "配置缺失", "未加载机制配置，无法自动计算时长。");
        return;
    }

    QStringList warnings;
    const QString mapPath = getProjectRoot() + "/config/plan_step_map.csv";
    const StepMappingMap stepMap = loadStepMappings(mapPath, &warnings);
    if (!warnings.isEmpty()) {
        qWarning() << "[PlanVisualizer] Step map warnings:" << warnings;
    }

    int updated = 0;
    int skipped = 0;
    for (auto it = m_durations.begin(); it != m_durations.end(); ++it) {
        const QString key = it.key();
        const StepMappingList mappings = resolveMappings(key, stepMap);
        if (mappings.isEmpty()) {
            ++skipped;
            continue;
        }

        double maxTimeSec = 0.0;
        bool computed = false;
        for (const StepMapping& mapping : mappings) {
            if (!mapping.valid()) {
                continue;
            }
            if (mapping.kind == StepKind::Spin || mapping.kind == StepKind::Hold) {
                continue;
            }

            const Mechanism::Code code = Mechanism::fromCodeString(mapping.axis);
            if (code < 0 || !configs.contains(code)) {
                continue;
            }

            const MechanismParams params = configs.value(code);
            if (params.controlMode.toLower() != "position") {
                continue;
            }

            double distance = 0.0;
            if (mapping.kind == StepKind::StepIndex) {
                if (params.anglePerPosition <= 0.0 || params.pulsesPerDegree <= 0.0) {
                    continue;
                }
                distance = std::abs(params.anglePerPosition * params.pulsesPerDegree);
            } else {
                if (!params.keyPositions.contains(mapping.start) ||
                    !params.keyPositions.contains(mapping.end)) {
                    continue;
                }
                distance = std::abs(params.keyPositions.value(mapping.end) - params.keyPositions.value(mapping.start));
            }

            const double speed = params.speed;
            double accel = params.acceleration;
            if (params.deceleration > 0.0) {
                accel = qMin(accel, params.deceleration);
            }
            const double timeSec = computeMoveTimeSec(distance, speed, accel);
            if (timeSec <= 0.0) {
                continue;
            }
            computed = true;
            maxTimeSec = qMax(maxTimeSec, timeSec);
        }

        if (!computed) {
            ++skipped;
            continue;
        }
        const int duration = qMax(1, static_cast<int>(std::ceil(maxTimeSec)));
        it.value() = duration;
        ++updated;
    }

    populateDurTable();
    if (updated > 0) {
        m_durationsModified = true;
    }
    ui->statusLabel->setText(QString("自动时长: 更新 %1, 跳过 %2").arg(updated).arg(skipped));
}

QString PlanVisualizerPage::getDurConfigPath() const
{
    return getProjectRoot() + "/config/durations.json";
}

bool PlanVisualizerPage::saveDurationsToJson(const QString& filePath, bool showErrors)
{
    QJsonObject obj;
    for (auto it = m_durations.constBegin(); it != m_durations.constEnd(); ++it) {
        obj[it.key()] = it.value();
    }
    QJsonDocument doc(obj);

    QFileInfo info(filePath);
    QDir dir(info.absolutePath());
    if (!dir.exists() && !dir.mkpath(".")) {
        if (showErrors) {
            QMessageBox::warning(this, "保存失败",
                QString("无法创建目录: %1").arg(info.absolutePath()));
        }
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (showErrors) {
            QMessageBox::warning(this, "保存失败",
                QString("无法写入文件: %1").arg(file.errorString()));
        }
        return false;
    }

    if (file.write(doc.toJson(QJsonDocument::Indented)) == -1) {
        if (showErrors) {
            QMessageBox::warning(this, "保存失败",
                QString("写入文件失败: %1").arg(file.errorString()));
        }
        return false;
    }

    if (!file.commit()) {
        if (showErrors) {
            QMessageBox::warning(this, "保存失败",
                QString("保存文件失败: %1").arg(file.errorString()));
        }
        return false;
    }

    return true;
}

bool PlanVisualizerPage::loadDurationsFromJson(const QString& filePath, bool showErrors)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (showErrors) {
            QMessageBox::warning(this, "加载失败",
                QString("无法打开文件: %1").arg(file.errorString()));
        }
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (showErrors) {
            QMessageBox::warning(this, "加载失败",
                QString("JSON解析错误: %1").arg(parseError.errorString()));
        }
        return false;
    }

    QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (it.value().isDouble()) {
            m_durations[it.key()] = it.value().toInt();
        }
    }

    return true;
}

void PlanVisualizerPage::writeDurationsToTempFile()
{
    QString tempPath = getProjectRoot() + "/python/multi_rig_plan/durations_override.json";
    if (saveDurationsToJson(tempPath, false)) {
        qDebug() << "Written durations to:" << tempPath;
    } else {
        qWarning() << "Failed to write durations to:" << tempPath;
    }
}

void PlanVisualizerPage::onExportJson()
{
    if (m_currentJson.isEmpty()) {
        QMessageBox::warning(this, "导出失败", "请先运行规划生成数据");
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(this,
        "导出JSON", "plan_output.json", "JSON文件 (*.json)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(m_currentJson).toJson(QJsonDocument::Indented));
        file.close();
        QMessageBox::information(this, "导出成功", QString("已导出到: %1").arg(fileName));
    } else {
        QMessageBox::warning(this, "导出失败", QString("无法写入文件: %1").arg(file.errorString()));
    }
}

QString PlanVisualizerPage::getPythonPath() const
{
    return "C:/Users/YMH/miniconda3/python.exe";
}

QString PlanVisualizerPage::getScriptPath(const QString& scriptName) const
{
    return getProjectRoot() + "/python/multi_rig_plan/" + scriptName;
}

void PlanVisualizerPage::runPythonScript(const QString& scriptName, int nPipes, int zoom)
{
    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
    }

    m_process = new QProcess(this);

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &PlanVisualizerPage::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &PlanVisualizerPage::onProcessError);
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &PlanVisualizerPage::onProcessReadyRead);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &PlanVisualizerPage::onProcessReadyRead);

    QString pythonPath = getPythonPath();
    QString scriptPath = getScriptPath(scriptName);

    QStringList args;
    args << scriptPath;
    args << QString("--n_pipes=%1").arg(nPipes);
    args << QString("--zoom=%1").arg(zoom);
    args << "--json";

    // 总是传递时长配置文件，确保使用最新参数
    QString configPath = getProjectRoot() + "/python/multi_rig_plan/durations_override.json";
    args << QString("--dur_config=%1").arg(configPath);

    qDebug() << "Running:" << pythonPath << args;
    m_process->start(pythonPath, args);
}

void PlanVisualizerPage::onProcessReadyRead()
{
    QString output = QString::fromUtf8(m_process->readAllStandardOutput());
    QString error = QString::fromUtf8(m_process->readAllStandardError());

    if (!output.isEmpty()) {
        ui->asciiOutput->appendPlainText(output);
    }
    if (!error.isEmpty()) {
        ui->asciiOutput->appendPlainText("[ERROR] " + error);
    }
}

void PlanVisualizerPage::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    ui->runButton->setEnabled(true);
    ui->replanButton->setEnabled(true);

    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        ui->statusLabel->setText("规划完成");
        m_currentAscii = ui->asciiOutput->toPlainText();

        parseAsciiOutput(m_currentAscii);
        m_currentJson = convertToJson(m_currentAscii);

        // 解析任务并更新甘特图
        QList<GanttTask> tasks = parseTasksFromAscii(m_currentAscii);
        QList<int> stageCuts = parseStageCutsFromAscii(m_currentAscii);
        updateGanttChart(tasks, stageCuts);
    } else {
        ui->statusLabel->setText(QString("运行失败 (退出码: %1)").arg(exitCode));
    }
}

void PlanVisualizerPage::onProcessError(QProcess::ProcessError error)
{
    ui->runButton->setEnabled(true);
    ui->replanButton->setEnabled(true);

    QString errorMsg;
    switch (error) {
    case QProcess::FailedToStart:
        errorMsg = "无法启动Python进程，请检查路径";
        break;
    case QProcess::Crashed:
        errorMsg = "Python进程崩溃";
        break;
    default:
        errorMsg = "执行出错";
    }

    ui->statusLabel->setText("错误: " + errorMsg);
    QMessageBox::warning(this, "执行错误", errorMsg);
}

void PlanVisualizerPage::parseAsciiOutput(const QString& output)
{
    QRegularExpression serialRe("Strict Serial Time\\s*:\\s*(\\d+)\\s*s");
    QRegularExpression optimizedRe("Optimized Time\\s*:\\s*(\\d+)\\s*s");

    auto serialMatch = serialRe.match(output);
    auto optimizedMatch = optimizedRe.match(output);

    if (serialMatch.hasMatch()) {
        m_serialTime = serialMatch.captured(1).toInt();
    }
    if (optimizedMatch.hasMatch()) {
        m_optimizedTime = optimizedMatch.captured(1).toInt();
    }

    if (!m_isOptimizedMode) {
        QRegularExpression tableEndRe("\\[\\s*\\d+,\\s*(\\d+)\\)");
        auto it = tableEndRe.globalMatch(output);
        int maxEnd = 0;
        while (it.hasNext()) {
            auto m = it.next();
            int end = m.captured(1).toInt();
            if (end > maxEnd) maxEnd = end;
        }
        if (maxEnd > 0) {
            m_serialTime = maxEnd;
            m_optimizedTime = 0;
        }
    }

    updateStatistics(m_serialTime, m_optimizedTime);
}

void PlanVisualizerPage::updateStatistics(int serialTime, int optimizedTime)
{
    ui->serialTimeLabel->setText(QString("串行时间: %1 s").arg(serialTime));

    if (optimizedTime > 0) {
        ui->optimizedTimeLabel->setText(QString("优化时间: %1 s").arg(optimizedTime));
        int saved = serialTime - optimizedTime;
        double percent = (serialTime > 0) ? (saved * 100.0 / serialTime) : 0;
        ui->savedTimeLabel->setText(QString("节省时间: %1 s (%2%)")
            .arg(saved).arg(percent, 0, 'f', 1));
    } else {
        ui->optimizedTimeLabel->setText("优化时间: --");
        ui->savedTimeLabel->setText("节省时间: --");
    }
}

QList<GanttTask> PlanVisualizerPage::parseTasksFromAscii(const QString& ascii)
{
    QList<GanttTask> tasks;
    QStringList lines = ascii.split("\n");

    for (const QString& line : lines) {
        // 查找DOF状态行 (格式: "Fz |XXXX....XXX| ")
        for (const QString& dof : DOFS) {
            if (line.trimmed().startsWith(dof + " |") || line.contains(QRegularExpression("^\\s*" + dof + "\\s*\\|"))) {
                int barStart = line.indexOf('|');
                int barEnd = line.lastIndexOf('|');
                if (barStart >= 0 && barEnd > barStart) {
                    QString timeline = line.mid(barStart + 1, barEnd - barStart - 1);

                    // 移除阶段分隔符
                    timeline.remove(" | ");
                    timeline.remove("|");

                    // 查找连续的X段
                    int taskId = 0;
                    int start = -1;
                    for (int i = 0; i < timeline.length(); i++) {
                        QChar c = timeline[i];
                        if (c == 'X' && start < 0) {
                            start = i;
                        } else if (c != 'X' && start >= 0) {
                            GanttTask task;
                            task.name = QString("%1_%2").arg(dof).arg(taskId++);
                            task.dof = dof;
                            task.start = start;
                            task.end = i;
                            task.opType = "move";
                            tasks.append(task);
                            start = -1;
                        }
                    }
                    if (start >= 0) {
                        GanttTask task;
                        task.name = QString("%1_%2").arg(dof).arg(taskId);
                        task.dof = dof;
                        task.start = start;
                        task.end = timeline.length();
                        task.opType = "move";
                        tasks.append(task);
                    }
                }
                break;
            }
        }
    }

    return tasks;
}

QList<int> PlanVisualizerPage::parseStageCutsFromAscii(const QString& ascii)
{
    QList<int> cuts;
    // 从第一行找分隔符位置
    QStringList lines = ascii.split("\n");
    for (const QString& line : lines) {
        if (line.contains("|") && (line.contains("X") || line.contains("."))) {
            int pos = 0;
            int cutIndex = 0;
            for (int i = 0; i < line.length(); i++) {
                if (i > 0 && line.mid(i, 3) == " | ") {
                    cuts.append(cutIndex);
                    i += 2;
                } else if (line[i] != ' ' && line[i] != '|') {
                    cutIndex++;
                }
            }
            break;
        }
    }
    return cuts;
}

void PlanVisualizerPage::updateGanttChart(const QList<GanttTask>& tasks, const QList<int>& stageCuts)
{
    m_ganttChart->clearPlottables();
    m_ganttChart->clearItems();

    if (tasks.isEmpty()) {
        ui->ganttHScrollBar->setRange(0, 0);
        m_ganttChart->replot();
        return;
    }

    // 计算最大时间
    int maxTime = 0;
    for (const GanttTask& task : tasks) {
        if (task.end > maxTime) maxTime = task.end;
    }

    // 绘制任务条
    for (const GanttTask& task : tasks) {
        int dofIndex = DOFS.indexOf(task.dof);
        if (dofIndex < 0) continue;

        QCPItemRect* rect = new QCPItemRect(m_ganttChart);
        rect->topLeft->setCoords(task.start, dofIndex + 0.35);
        rect->bottomRight->setCoords(task.end, dofIndex - 0.35);

        QColor color = DOF_COLORS.value(task.dof, Qt::gray);
        rect->setPen(QPen(color.darker(120)));
        rect->setBrush(QBrush(color));

        // 工具提示（通过设置名称，可以在点击事件中获取）
        rect->setProperty("task_name", task.name);
    }

    // 绘制阶段分隔线
    QStringList stageLabels = {"A", "B", "C", "D"};
    int labelIndex = 0;
    for (int cut : stageCuts) {
        QCPItemLine* line = new QCPItemLine(m_ganttChart);
        line->start->setCoords(cut, -0.5);
        line->end->setCoords(cut, DOFS.size() - 0.5);
        line->setPen(QPen(Qt::red, 1, Qt::DashLine));

        // 阶段标签
        if (labelIndex < stageLabels.size()) {
            QCPItemText* label = new QCPItemText(m_ganttChart);
            label->position->setCoords(cut + 2, DOFS.size() - 0.2);
            label->setText(stageLabels[labelIndex]);
            label->setFont(QFont("Arial", 10, QFont::Bold));
            label->setColor(Qt::red);
            labelIndex++;
        }
    }

    // 计算可见区域宽度（显示约50秒的时间范围）
    int visibleRange = 50;
    if (maxTime <= visibleRange) {
        // 数据量小，全部显示
        m_ganttChart->xAxis->setRange(0, maxTime + 5);
        ui->ganttHScrollBar->setRange(0, 0);
        ui->ganttHScrollBar->setEnabled(false);
    } else {
        // 数据量大，启用滚动条
        m_ganttChart->xAxis->setRange(0, visibleRange);
        ui->ganttHScrollBar->setRange(0, maxTime - visibleRange + 10);
        ui->ganttHScrollBar->setPageStep(visibleRange);
        ui->ganttHScrollBar->setSingleStep(5);
        ui->ganttHScrollBar->setValue(0);
        ui->ganttHScrollBar->setEnabled(true);
    }

    m_ganttChart->yAxis->setRange(-0.5, DOFS.size() - 0.5);
    m_ganttChart->replot();
}

QJsonObject PlanVisualizerPage::convertToJson(const QString& asciiOutput)
{
    QJsonObject result;

    result["mode"] = m_isOptimizedMode ? "optimized" : "serial";
    result["n_pipes"] = ui->pipesSpinBox->value();
    result["serial_time"] = m_serialTime;
    result["optimized_time"] = m_optimizedTime;

    QJsonObject durConfig;
    for (auto it = m_durations.constBegin(); it != m_durations.constEnd(); ++it) {
        durConfig[it.key()] = it.value();
    }
    result["durations"] = durConfig;

    QList<GanttTask> tasks = parseTasksFromAscii(asciiOutput);
    QJsonArray tasksArray;
    for (const GanttTask& task : tasks) {
        QJsonObject taskObj;
        taskObj["id"] = task.name;
        taskObj["dof"] = task.dof;
        taskObj["start"] = task.start;
        taskObj["end"] = task.end;
        taskObj["duration"] = task.end - task.start;
        taskObj["op_type"] = task.opType;
        tasksArray.append(taskObj);
    }
    result["tasks"] = tasksArray;
    result["ascii"] = asciiOutput;

    return result;
}
