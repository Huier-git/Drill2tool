#include "ui/DatabasePage.h"
#include "ui_DatabasePage.h"
#include <QDateTime>
#include <QMessageBox>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QDebug>
#include <QFileDialog>
#include <QProgressDialog>
#include <QTextStream>
#include <QtConcurrent>
#include "dataACQ/DataTypes.h"

DatabasePage::DatabasePage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DatabasePage)
    , m_querier(nullptr)
    , m_scalarPlot(nullptr)
    , m_vibrationPlot(nullptr)
    , m_currentRoundId(-1)
    , m_currentRoundStartUs(0)
    , m_currentRoundDurationSec(0)
{
    ui->setupUi(this);

    m_querier = new DataQuerier("database/drill_data.db", this);
    if (!m_querier->initialize()) {
        qWarning() << "DataQuerier初始化失败";
    }

    // 初始化图表
    setupPlots();

    // 连接信号
    connect(ui->btn_refresh_rounds, &QPushButton::clicked, this, &DatabasePage::onRefreshRounds);
    connect(ui->btn_select_round, &QPushButton::clicked, this, &DatabasePage::onRoundSelected);
    connect(ui->table_rounds, &QTableWidget::itemDoubleClicked, this, &DatabasePage::onRoundSelected);
    connect(ui->btn_query, &QPushButton::clicked, this, &DatabasePage::onQuery);
    connect(ui->btn_exec_sql, &QPushButton::clicked, this, &DatabasePage::onExecSql);

    // 新增：异步查询信号
    connect(&m_queryWatcher, &QFutureWatcher<QList<DataQuerier::WindowData>>::finished,
            this, &DatabasePage::onQueryFinished);

    // 新增：导出按钮
    connect(ui->btn_export, &QPushButton::clicked,
            this, &DatabasePage::onExportClicked);

    // 快捷选择
    connect(ui->btn_select_all, &QPushButton::clicked, this, &DatabasePage::onSelectAll);
    connect(ui->btn_first_10, &QPushButton::clicked, this, &DatabasePage::onSelectFirst10);
    connect(ui->btn_last_10, &QPushButton::clicked, this, &DatabasePage::onSelectLast10);

    // 时间范围变化
    connect(ui->spin_start_sec, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &DatabasePage::onStartSecChanged);
    connect(ui->spin_end_sec, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &DatabasePage::onEndSecChanged);

    loadRoundsList();
}

DatabasePage::~DatabasePage()
{
    delete ui;
}

void DatabasePage::onRefreshRounds()
{
    loadRoundsList();
}

void DatabasePage::loadRoundsList()
{
    if (!m_querier) return;

    ui->table_rounds->setRowCount(0);
    QList<DataQuerier::RoundInfo> rounds = m_querier->getAllRounds();

    for (const auto &round : rounds) {
        int row = ui->table_rounds->rowCount();
        ui->table_rounds->insertRow(row);

        ui->table_rounds->setItem(row, 0, new QTableWidgetItem(QString::number(round.roundId)));

        QDateTime startDt = QDateTime::fromMSecsSinceEpoch(round.startTimeUs / 1000);
        ui->table_rounds->setItem(row, 1, new QTableWidgetItem(startDt.toString("MM-dd HH:mm:ss")));

        // 计算时长
        qint64 durationSec = (round.endTimeUs - round.startTimeUs) / 1000000;
        if (durationSec < 0) durationSec = 0;

        QString durationStr;
        if (durationSec < 60) {
            durationStr = QString("%1秒").arg(durationSec);
        } else if (durationSec < 3600) {
            durationStr = QString("%1分%2秒").arg(durationSec / 60).arg(durationSec % 60);
        } else {
            durationStr = QString("%1时%2分").arg(durationSec / 3600).arg((durationSec % 3600) / 60);
        }
        ui->table_rounds->setItem(row, 2, new QTableWidgetItem(durationStr));
        ui->table_rounds->setItem(row, 3, new QTableWidgetItem(round.status));

        // 存储额外数据
        ui->table_rounds->item(row, 0)->setData(Qt::UserRole, round.startTimeUs);
        ui->table_rounds->item(row, 0)->setData(Qt::UserRole + 1, durationSec);
    }

    ui->table_rounds->resizeColumnsToContents();
}

void DatabasePage::onRoundSelected()
{
    int row = ui->table_rounds->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "提示", "请先在表格中点击选择一个轮次");
        return;
    }

    m_currentRoundId = ui->table_rounds->item(row, 0)->text().toInt();
    m_currentRoundStartUs = ui->table_rounds->item(row, 0)->data(Qt::UserRole).toLongLong();
    m_currentRoundDurationSec = ui->table_rounds->item(row, 0)->data(Qt::UserRole + 1).toLongLong();

    updateRoundInfo(m_currentRoundId, m_currentRoundDurationSec);
}

void DatabasePage::updateRoundInfo(int roundId, qint64 durationSec)
{
    // 更新显示信息
    QString info = QString("轮次 %1 | 总时长: %2 秒").arg(roundId).arg(durationSec);
    ui->label_round_info->setText(info);

    // 更新SpinBox范围
    ui->spin_start_sec->setMaximum(durationSec);
    ui->spin_end_sec->setMaximum(durationSec);
    ui->spin_start_sec->setValue(0);
    ui->spin_end_sec->setValue(durationSec);

    // 更新Slider
    ui->slider_range->setMaximum(durationSec);
    ui->slider_range->setValue(durationSec);
}

void DatabasePage::onStartSecChanged(int value)
{
    // 确保起始不大于结束
    if (value > ui->spin_end_sec->value()) {
        ui->spin_end_sec->setValue(value);
    }
}

void DatabasePage::onEndSecChanged(int value)
{
    // 确保结束不小于起始
    if (value < ui->spin_start_sec->value()) {
        ui->spin_start_sec->setValue(value);
    }
    ui->slider_range->setValue(value);
}

void DatabasePage::onSelectAll()
{
    if (m_currentRoundDurationSec <= 0) return;
    ui->spin_start_sec->setValue(0);
    ui->spin_end_sec->setValue(m_currentRoundDurationSec);
}

void DatabasePage::onSelectFirst10()
{
    if (m_currentRoundDurationSec <= 0) return;
    ui->spin_start_sec->setValue(0);
    ui->spin_end_sec->setValue(qMin((qint64)10, m_currentRoundDurationSec));
}

void DatabasePage::onSelectLast10()
{
    if (m_currentRoundDurationSec <= 0) return;
    qint64 start = qMax((qint64)0, m_currentRoundDurationSec - 10);
    ui->spin_start_sec->setValue(start);
    ui->spin_end_sec->setValue(m_currentRoundDurationSec);
}

void DatabasePage::onQuery()
{
    if (!m_querier) return;

    if (m_currentRoundId < 0) {
        QMessageBox::warning(this, "提示", "请先选择一个轮次");
        return;
    }

    // 取消正在运行的查询
    if (m_queryWatcher.isRunning()) {
        m_queryWatcher.cancel();
        m_queryWatcher.waitForFinished();
    }

    // 禁用按钮
    ui->btn_query->setEnabled(false);
    ui->btn_query->setText("查询中...");
    ui->btn_export->setEnabled(false);

    int startSec = ui->spin_start_sec->value();
    int endSec = ui->spin_end_sec->value();

    // 计算实际的微秒时间戳
    qint64 startUs = m_currentRoundStartUs + (qint64)startSec * 1000000;
    qint64 endUs = m_currentRoundStartUs + (qint64)endSec * 1000000;
    int roundId = m_currentRoundId;

    // 启动异步查询
    QFuture<QList<DataQuerier::WindowData>> future = QtConcurrent::run([roundId, startUs, endUs]() {
        DataQuerier tempQuerier("database/drill_data.db");
        if (tempQuerier.initialize()) {
            return tempQuerier.getTimeRangeData(roundId, startUs, endUs);
        }
        return QList<DataQuerier::WindowData>();
    });

    m_queryWatcher.setFuture(future);
}

void DatabasePage::displayQueryResult(const QList<DataQuerier::WindowData> &dataList)
{
    ui->table_result->clear();
    ui->table_result->setColumnCount(6);
    ui->table_result->setHorizontalHeaderLabels({"时间(秒)", "振动X", "振动Y", "振动Z", "MDB", "电机"});
    ui->table_result->setRowCount(dataList.size());

    for (int i = 0; i < dataList.size(); ++i) {
        const auto &data = dataList[i];

        // 计算相对时间（秒）
        qint64 relativeSec = (data.windowStartUs - m_currentRoundStartUs) / 1000000;
        ui->table_result->setItem(i, 0, new QTableWidgetItem(QString::number(relativeSec)));

        // 振动数据采样点数
        for (int ch = 0; ch < 3; ++ch) {
            int count = data.vibrationData.contains(ch) ? data.vibrationData[ch].size() : 0;
            ui->table_result->setItem(i, 1 + ch, new QTableWidgetItem(QString::number(count)));
        }

        // MDB数据
        int mdbCount = 0;
        for (auto it = data.scalarData.begin(); it != data.scalarData.end(); ++it) {
            if (it.key() >= 100 && it.key() < 200) mdbCount += it.value().size();
        }
        ui->table_result->setItem(i, 4, new QTableWidgetItem(QString::number(mdbCount)));

        // 电机数据
        int motorCount = 0;
        for (auto it = data.scalarData.begin(); it != data.scalarData.end(); ++it) {
            if (it.key() >= 300 && it.key() < 400) motorCount += it.value().size();
        }
        ui->table_result->setItem(i, 5, new QTableWidgetItem(QString::number(motorCount)));
    }

    ui->table_result->resizeColumnsToContents();
    ui->label_result_info->setText(QString("共 %1 个时间窗口 (每窗口1秒)").arg(dataList.size()));
}

void DatabasePage::onExecSql()
{
    QString sql = ui->te_sql->toPlainText().trimmed();
    if (sql.isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入SQL语句");
        return;
    }

    QSqlQuery query(m_querier->database());
    if (!query.exec(sql)) {
        QMessageBox::critical(this, "SQL错误", query.lastError().text());
        return;
    }

    // 显示结果
    ui->table_result->clear();

    QSqlRecord record = query.record();
    int colCount = record.count();
    ui->table_result->setColumnCount(colCount);

    QStringList headers;
    for (int i = 0; i < colCount; ++i) {
        headers << record.fieldName(i);
    }
    ui->table_result->setHorizontalHeaderLabels(headers);

    int rowCount = 0;
    while (query.next()) {
        ui->table_result->insertRow(rowCount);
        for (int i = 0; i < colCount; ++i) {
            ui->table_result->setItem(rowCount, i, new QTableWidgetItem(query.value(i).toString()));
        }
        rowCount++;
        if (rowCount >= 1000) break;  // 限制最大行数
    }

    ui->label_result_info->setText(QString("共 %1 条记录").arg(rowCount));
}

// ==================================================
// 辅助函数：传感器类型转字符串
// ==================================================
static QString sensorTypeToString(int sensorType)
{
    switch ((SensorType)sensorType) {
        case SensorType::Vibration_X: return "振动X";
        case SensorType::Vibration_Y: return "振动Y";
        case SensorType::Vibration_Z: return "振动Z";
        case SensorType::Torque_MDB: return "扭矩(MDB)";
        case SensorType::Force_Upper: return "上拉力(MDB)";
        case SensorType::Force_Lower: return "下拉力(MDB)";
        case SensorType::Position_MDB: return "位置(MDB)";
        case SensorType::Motor_Position: return "电机位置";
        case SensorType::Motor_Speed: return "电机速度";
        case SensorType::Motor_Torque: return "电机扭矩";
        case SensorType::Motor_Current: return "电机电流";
        default: return QString("传感器%1").arg(sensorType);
    }
}

// ==================================================
// 图表初始化
// ==================================================
void DatabasePage::setupPlots()
{
    // 标量数据图表
    m_scalarPlot = ui->plot_scalar;
    if (m_scalarPlot) {
        m_scalarPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
        m_scalarPlot->legend->setVisible(true);
        m_scalarPlot->legend->setFont(QFont("Microsoft YaHei", 9));
        m_scalarPlot->xAxis->setLabel("时间 (秒)");
        m_scalarPlot->yAxis->setLabel("数值");
        m_scalarPlot->axisRect()->setupFullAxesBox();
    }

    // 振动数据图表
    m_vibrationPlot = ui->plot_vibration;
    if (m_vibrationPlot) {
        m_vibrationPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
        m_vibrationPlot->legend->setVisible(true);
        m_vibrationPlot->legend->setFont(QFont("Microsoft YaHei", 9));
        m_vibrationPlot->xAxis->setLabel("时间 (秒)");
        m_vibrationPlot->yAxis->setLabel("RMS值");
        m_vibrationPlot->axisRect()->setupFullAxesBox();
    }
}

// ==================================================
// 异步查询完成处理
// ==================================================
void DatabasePage::onQueryFinished()
{
    // 恢复按钮
    ui->btn_query->setEnabled(true);
    ui->btn_query->setText("查询数据");
    ui->btn_export->setEnabled(true);

    if (m_queryWatcher.isCanceled()) {
        return;
    }

    // 获取结果
    QList<DataQuerier::WindowData> dataList = m_queryWatcher.result();

    // 更新显示
    displayQueryResult(dataList);
    updateScalarPlot(dataList);
    updateVibrationPlot(dataList);
}

// ==================================================
// 更新标量数据图表
// ==================================================
void DatabasePage::updateScalarPlot(const QList<DataQuerier::WindowData> &dataList)
{
    if (!m_scalarPlot) return;

    m_scalarPlot->clearGraphs();

    if (dataList.isEmpty()) {
        m_scalarPlot->replot();
        return;
    }

    // 按传感器类型分组数据
    QMap<int, QVector<double>> xData;
    QMap<int, QVector<double>> yData;

    for (const auto &window : dataList) {
        double winStartSec = (window.windowStartUs - m_currentRoundStartUs) / 1000000.0;

        for (auto it = window.scalarData.begin(); it != window.scalarData.end(); ++it) {
            int sensorType = it.key();
            const QVector<double>& values = it.value();

            if (values.isEmpty()) continue;

            // 为这个窗口内的每个样本生成时间点
            double step = 1.0 / values.size();
            for (int i = 0; i < values.size(); ++i) {
                xData[sensorType].append(winStartSec + i * step);
                yData[sensorType].append(values[i]);
            }
        }
    }

    // 创建图表
    QVector<QColor> colors = {
        QColor("#409eff"), QColor("#67c23a"), QColor("#e6a23c"),
        QColor("#f56c6c"), QColor("#909399"), QColor("#8e44ad"),
        QColor("#16a085"), QColor("#2c3e50")
    };

    int colorIndex = 0;
    for (auto it = xData.begin(); it != xData.end(); ++it) {
        int type = it.key();
        m_scalarPlot->addGraph();
        m_scalarPlot->graph()->setData(it.value(), yData[type]);
        m_scalarPlot->graph()->setName(sensorTypeToString(type));
        m_scalarPlot->graph()->setPen(QPen(colors[colorIndex % colors.size()], 1.5));
        colorIndex++;
    }

    m_scalarPlot->rescaleAxes();
    m_scalarPlot->replot();
}

// ==================================================
// 更新振动数据图表
// ==================================================
void DatabasePage::updateVibrationPlot(const QList<DataQuerier::WindowData> &dataList)
{
    if (!m_vibrationPlot) return;

    m_vibrationPlot->clearGraphs();

    if (dataList.isEmpty() || m_currentRoundId < 0) {
        m_vibrationPlot->replot();
        return;
    }

    int startSec = ui->spin_start_sec->value();
    int endSec = ui->spin_end_sec->value();
    qint64 startUs = m_currentRoundStartUs + (qint64)startSec * 1000000;
    qint64 endUs = m_currentRoundStartUs + (qint64)endSec * 1000000;

    // 为3个通道创建图表
    QVector<QColor> colors = {Qt::red, Qt::darkGreen, Qt::blue};
    QStringList names = {"X轴 RMS", "Y轴 RMS", "Z轴 RMS"};

    for (int channel = 0; channel < 3; ++channel) {
        QList<DataQuerier::VibrationStats> stats = m_querier->getVibrationStats(
            m_currentRoundId, channel, startUs, endUs
        );

        QVector<double> x, y;
        for (const auto &s : stats) {
            x.append((s.timestampUs - m_currentRoundStartUs) / 1000000.0);
            y.append(s.rmsValue);
        }

        if (!x.isEmpty()) {
            m_vibrationPlot->addGraph();
            m_vibrationPlot->graph()->setData(x, y);
            m_vibrationPlot->graph()->setName(names[channel]);
            m_vibrationPlot->graph()->setPen(QPen(colors[channel], 1.5));
        }
    }

    m_vibrationPlot->rescaleAxes();
    m_vibrationPlot->replot();
}

// ==================================================
// 导出数据
// ==================================================
void DatabasePage::onExportClicked()
{
    if (m_currentRoundId < 0) {
        QMessageBox::warning(this, "提示", "请先选择轮次并查询数据");
        return;
    }

    int startSec = ui->spin_start_sec->value();
    int endSec = ui->spin_end_sec->value();
    QString defaultName = QString("round_%1_%2s-%3s.csv")
        .arg(m_currentRoundId)
        .arg(startSec)
        .arg(endSec);

    QString filePath = QFileDialog::getSaveFileName(
        this,
        "导出数据",
        defaultName,
        "CSV 文件 (*.csv);;所有文件 (*.*)"
    );

    if (filePath.isEmpty()) {
        return;
    }

    startExportAsync(filePath);
}

void DatabasePage::startExportAsync(const QString& filePath)
{
    QProgressDialog* progress = new QProgressDialog(
        "正在导出数据...",
        "取消",
        0, 100,
        this
    );
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setValue(0);

    int startSec = ui->spin_start_sec->value();
    int endSec = ui->spin_end_sec->value();
    qint64 startUs = m_currentRoundStartUs + (qint64)startSec * 1000000;
    qint64 endUs = m_currentRoundStartUs + (qint64)endSec * 1000000;
    int roundId = m_currentRoundId;
    qint64 roundStartUs = m_currentRoundStartUs;

    // 在后台线程执行导出
    QtConcurrent::run([this, roundId, startUs, endUs, filePath, progress, roundStartUs]() {
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMetaObject::invokeMethod(progress, "close", Qt::QueuedConnection);
            QMetaObject::invokeMethod(this, [this, filePath]() {
                QMessageBox::critical(this, "错误",
                    QString("无法创建文件: %1").arg(filePath));
            }, Qt::QueuedConnection);
            return;
        }

        QTextStream out(&file);
        out.setCodec("UTF-8");

        // 写入头部
        out << "# Round ID: " << roundId << "\n";
        out << "# Time Range: " << (startUs - roundStartUs) / 1e6
            << " - " << (endUs - roundStartUs) / 1e6 << " seconds\n";
        out << "# Export Time: " << QDateTime::currentDateTime().toString() << "\n";
        out << "timestamp_us,sensor_type,value\n";

        // 创建临时查询器
        DataQuerier tempQuerier("database/drill_data.db");
        if (!tempQuerier.initialize()) {
            file.close();
            QMetaObject::invokeMethod(progress, "close", Qt::QueuedConnection);
            return;
        }

        auto dataList = tempQuerier.getTimeRangeData(roundId, startUs, endUs);

        for (int i = 0; i < dataList.size(); ++i) {
            const auto& window = dataList[i];

            // 写入标量数据
            for (auto it = window.scalarData.begin(); it != window.scalarData.end(); ++it) {
                int sensorType = it.key();
                for (double value : it.value()) {
                    out << window.windowStartUs << ","
                        << sensorType << ","
                        << value << "\n";
                }
            }

            // 更新进度
            int percent = (i + 1) * 100 / dataList.size();
            QMetaObject::invokeMethod(progress, "setValue",
                Qt::QueuedConnection, Q_ARG(int, percent));
        }

        file.close();

        // 完成
        QMetaObject::invokeMethod(progress, "close", Qt::QueuedConnection);
        QMetaObject::invokeMethod(this, [this]() {
            QMessageBox::information(this, "完成", "数据导出成功");
        }, Qt::QueuedConnection);
    });
}
