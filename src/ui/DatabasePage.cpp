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
#include <cmath>
#include "dataACQ/DataTypes.h"

DatabasePage::DatabasePage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DatabasePage)
    , m_querier(nullptr)
    , m_scalarPlot(nullptr)
    , m_cursorLine(nullptr)
    , m_currentRoundId(-1)
    , m_currentRoundStartUs(0)
    , m_currentRoundDurationSec(0)
    , m_dbPath("D:/KT_DrillControl/drill_data.db")
{
    ui->setupUi(this);

    m_querier = new DataQuerier(m_dbPath, this);
    if (!m_querier->initialize()) {
        qWarning() << "DataQuerier初始化失败";
    }

    // 初始化图表
    setupPlots();

    // 连接信号
    connect(ui->btn_refresh_rounds, &QPushButton::clicked, this, &DatabasePage::onRefreshRounds);
    connect(ui->btn_select_round, &QPushButton::clicked, this, &DatabasePage::onRoundSelected);
    connect(ui->btn_delete_round, &QPushButton::clicked, this, &DatabasePage::onDeleteRound);
    connect(ui->table_rounds, &QTableWidget::itemDoubleClicked, this, &DatabasePage::onRoundSelected);
    connect(ui->btn_query, &QPushButton::clicked, this, &DatabasePage::onQuery);
    connect(ui->btn_exec_sql, &QPushButton::clicked, this, &DatabasePage::onExecSql);

    // 异步查询信号
    connect(&m_queryWatcher, &QFutureWatcher<QList<DataQuerier::WindowData>>::finished,
            this, &DatabasePage::onQueryFinished);

    // 导出按钮
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

    // 数据类型筛选变化 - 重新绘制图表
    connect(ui->combo_data_type, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        if (!m_currentQueryData.isEmpty()) {
            updateScalarPlot(m_currentQueryData);
        }
    });

    // 图表交互
    connect(ui->table_result, &QTableWidget::currentCellChanged,
            this, [this](int row, int, int, int) {
        if (row >= 0) onTableRowSelected();
    });

    // 设置按钮样式类型
    ui->btn_query->setProperty("type", "info");  // 查询数据 - 浅蓝色
    ui->btn_export->setProperty("type", "info");  // 导出数据 - 浅蓝色
    ui->btn_refresh_rounds->setProperty("type", "primary");  // 刷新轮次 - 蓝色

    // 刷新按钮样式
    ui->btn_query->style()->unpolish(ui->btn_query);
    ui->btn_query->style()->polish(ui->btn_query);
    ui->btn_export->style()->unpolish(ui->btn_export);
    ui->btn_export->style()->polish(ui->btn_export);
    ui->btn_refresh_rounds->style()->unpolish(ui->btn_refresh_rounds);
    ui->btn_refresh_rounds->style()->polish(ui->btn_refresh_rounds);

    loadRoundsList();
}

DatabasePage::~DatabasePage()
{
    delete ui;
}

void DatabasePage::setDatabasePath(const QString &dbPath)
{
    if (dbPath.isEmpty() || dbPath == m_dbPath) {
        return;
    }

    m_dbPath = dbPath;
    if (m_querier) {
        delete m_querier;
        m_querier = nullptr;
    }

    m_querier = new DataQuerier(m_dbPath, this);
    if (!m_querier->initialize()) {
        qWarning() << "DataQuerier初始化失败";
    }
    loadRoundsList();
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

        // 计算实际时长（从time_windows表）
        qint64 durationSec = m_querier->getRoundActualDuration(round.roundId);

        QString durationStr;
        if (durationSec <= 0) {
            durationStr = "无数据";
        } else if (durationSec < 60) {
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
    m_currentRoundDurationSec = ui->table_rounds->item(row, 0)->data(Qt::UserRole + 1).toLongLong();

    // 使用轮次的真实开始时间作为时间基准（而非第一个窗口的时间戳）
    // 这样可以确保所有数据类型的时间轴对齐，无论单独查询还是一起查询
    m_currentRoundStartUs = ui->table_rounds->item(row, 0)->data(Qt::UserRole).toLongLong();

    updateRoundInfo(m_currentRoundId, m_currentRoundDurationSec);
}

void DatabasePage::onDeleteRound()
{
    // 检查是否选中了轮次
    int row = ui->table_rounds->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "警告", "请先在表格中选择要删除的轮次");
        return;
    }

    // 获取选中轮次的信息
    int roundId = ui->table_rounds->item(row, 0)->text().toInt();
    QString startTime = ui->table_rounds->item(row, 1)->text();
    QString duration = ui->table_rounds->item(row, 2)->text();
    QString status = ui->table_rounds->item(row, 3)->text();

    // 显示确认对话框（警告风格）
    QMessageBox confirmBox(this);
    confirmBox.setIcon(QMessageBox::Warning);
    confirmBox.setWindowTitle("⚠️ 危险操作");
    confirmBox.setText(QString("确认要删除轮次 %1 吗？").arg(roundId));
    confirmBox.setInformativeText(QString(
        "轮次信息：\n"
        "• 开始时间：%1\n"
        "• 持续时间：%2\n"
        "• 状态：%3\n\n"
        "⚠️ 警告：此操作将永久删除该轮次的所有数据，包括：\n"
        "  - 所有传感器采集数据\n"
        "  - 振动数据记录\n"
        "  - 时间窗口信息\n"
        "  - 事件日志\n\n"
        "此操作不可恢复！"
    ).arg(startTime).arg(duration).arg(status));

    confirmBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    confirmBox.setDefaultButton(QMessageBox::No);
    confirmBox.button(QMessageBox::Yes)->setText("确认删除");
    confirmBox.button(QMessageBox::No)->setText("取消");

    if (confirmBox.exec() != QMessageBox::Yes) {
        return;
    }

    // 执行删除操作
    QSqlDatabase db = m_querier->database();
    if (!db.isOpen()) {
        QMessageBox::critical(this, "错误", "数据库未打开");
        return;
    }

    // 开始事务
    if (!db.transaction()) {
        QMessageBox::critical(this, "错误", "无法开始事务：" + db.lastError().text());
        return;
    }

    QSqlQuery query(db);

    // 删除标量数据
    query.prepare("DELETE FROM scalar_samples WHERE round_id = ?");
    query.addBindValue(roundId);
    if (!query.exec()) {
        db.rollback();
        QMessageBox::critical(this, "错误", "删除标量数据失败：" + query.lastError().text());
        return;
    }
    int deletedScalar = query.numRowsAffected();

    // 删除振动数据
    query.prepare("DELETE FROM vibration_blocks WHERE round_id = ?");
    query.addBindValue(roundId);
    if (!query.exec()) {
        db.rollback();
        QMessageBox::critical(this, "错误", "删除振动数据失败：" + query.lastError().text());
        return;
    }
    int deletedVibration = query.numRowsAffected();

    // 删除时间窗口
    query.prepare("DELETE FROM time_windows WHERE round_id = ?");
    query.addBindValue(roundId);
    if (!query.exec()) {
        db.rollback();
        QMessageBox::critical(this, "错误", "删除时间窗口失败：" + query.lastError().text());
        return;
    }
    int deletedWindows = query.numRowsAffected();

    // 删除事件记录
    query.prepare("DELETE FROM events WHERE round_id = ?");
    query.addBindValue(roundId);
    if (!query.exec()) {
        db.rollback();
        QMessageBox::critical(this, "错误", "删除事件记录失败：" + query.lastError().text());
        return;
    }
    int deletedEvents = query.numRowsAffected();

    // 删除频率日志
    query.prepare("DELETE FROM frequency_log WHERE round_id = ?");
    query.addBindValue(roundId);
    if (!query.exec()) {
        db.rollback();
        QMessageBox::critical(this, "错误", "删除频率日志失败：" + query.lastError().text());
        return;
    }

    // 删除轮次记录
    query.prepare("DELETE FROM rounds WHERE round_id = ?");
    query.addBindValue(roundId);
    if (!query.exec()) {
        db.rollback();
        QMessageBox::critical(this, "错误", "删除轮次记录失败：" + query.lastError().text());
        return;
    }

    // 提交事务
    if (!db.commit()) {
        db.rollback();
        QMessageBox::critical(this, "错误", "提交事务失败：" + db.lastError().text());
        return;
    }

    // 显示成功消息
    QString successMsg = QString(
        "轮次 %1 删除成功！\n\n"
        "已删除数据：\n"
        "• 标量样本：%2 条\n"
        "• 振动数据块：%3 条\n"
        "• 时间窗口：%4 个\n"
        "• 事件记录：%5 条"
    ).arg(roundId).arg(deletedScalar).arg(deletedVibration).arg(deletedWindows).arg(deletedEvents);

    QMessageBox::information(this, "成功", successMsg);

    // 刷新轮次列表
    loadRoundsList();

    // 如果删除的是当前选中的轮次，清空当前轮次信息
    if (roundId == m_currentRoundId) {
        m_currentRoundId = -1;
        ui->label_round_info->setText("请选择一个轮次");
    }
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
    QString dbPath = m_dbPath;

    // 计算实际的微秒时间戳
    qint64 startUs = m_currentRoundStartUs + (qint64)startSec * 1000000;
    qint64 endUs = m_currentRoundStartUs + (qint64)endSec * 1000000;
    int roundId = m_currentRoundId;

    // 启动异步查询
    QFuture<QList<DataQuerier::WindowData>> future = QtConcurrent::run([roundId, startUs, endUs, dbPath]() {
        DataQuerier tempQuerier(dbPath);
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
    // 处理电机数据的组合键 (sensorType * 100 + motorId)
    // 例如：30002 = Motor_Position for motor 2
    if (sensorType >= 30000 && sensorType < 40000) {
        int baseSensorType = sensorType / 100;  // 300, 301, 302, 303
        int motorId = sensorType % 100;         // 0-7
        QString typeName;
        switch (baseSensorType) {
            case 300: typeName = "位置"; break;
            case 301: typeName = "速度"; break;
            case 302: typeName = "扭矩"; break;
            case 303: typeName = "电流"; break;
            default: typeName = "未知"; break;
        }
        return QString("电机%1%2").arg(motorId).arg(typeName);
    }

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
// 辅助函数：传感器类型转单位
// ==================================================
static QString sensorTypeToUnit(int sensorType)
{
    // 处理电机数据的组合键
    if (sensorType >= 30000 && sensorType < 40000) {
        int baseSensorType = sensorType / 100;
        switch (baseSensorType) {
            case 300: return "脉冲";    // Motor_Position
            case 301: return "脉冲/s";  // Motor_Speed
            case 302: return "%";       // Motor_Torque
            case 303: return "A";       // Motor_Current
            default: return "";
        }
    }

    switch ((SensorType)sensorType) {
        case SensorType::Vibration_X:
        case SensorType::Vibration_Y:
        case SensorType::Vibration_Z:
            return "g";  // 重力加速度
        case SensorType::Torque_MDB:
            return "N·m";  // 牛顿米
        case SensorType::Force_Upper:
        case SensorType::Force_Lower:
            return "N";  // 牛顿
        case SensorType::Position_MDB:
            return "mm";  // 毫米
        case SensorType::Motor_Position:
            return "脉冲";  // 编码器脉冲
        case SensorType::Motor_Speed:
            return "脉冲/s";  // 脉冲每秒
        case SensorType::Motor_Torque:
            return "%";  // 扭矩百分比
        case SensorType::Motor_Current:
            return "A";  // 安培
        default:
            return "";
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
        configureChartDarkTheme(m_scalarPlot);
        m_scalarPlot->xAxis->setLabel("时间 (秒)");
        m_scalarPlot->yAxis->setLabel("数值");
        m_scalarPlot->legend->setVisible(true);
        m_scalarPlot->legend->setFont(QFont("Microsoft YaHei", 9));
        m_scalarPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
        m_scalarPlot->axisRect()->setupFullAxesBox();

        // 图表点击事件
        connect(m_scalarPlot, &QCustomPlot::mousePress,
                this, &DatabasePage::onScalarPlotClicked);

        // 创建游标线（用于图表-表格同步）
        m_cursorLine = new QCPItemLine(m_scalarPlot);
        m_cursorLine->setVisible(false);
        QPen cursorPen(QColor(255, 170, 0), 2, Qt::DashLine);  // 橙色虚线
        m_cursorLine->setPen(cursorPen);
        m_cursorLine->start->setType(QCPItemPosition::ptAxisRectRatio);
        m_cursorLine->start->setCoords(0, 0);  // 顶部
        m_cursorLine->end->setType(QCPItemPosition::ptAxisRectRatio);
        m_cursorLine->end->setCoords(0, 1);    // 底部
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

    // 获取结果并保存
    m_currentQueryData = m_queryWatcher.result();

    // 更新显示
    displayQueryResult(m_currentQueryData);
    updateScalarPlot(m_currentQueryData);
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

    // 获取当前筛选类型
    int filterType = ui->combo_data_type->currentIndex();
    // 0=全部, 1=振动(VK701), 2=MDB(100-103), 3=电机(300-303)

    // 根据筛选类型设置Y轴标签
    QString yAxisLabel;
    switch (filterType) {
        case 1:  // 振动
            yAxisLabel = "振动加速度 RMS (g)";
            break;
        case 2:  // MDB
            yAxisLabel = "数值 (N / N·m / mm)";
            break;
        case 3:  // 电机（三环：位置/速度/电流）
            yAxisLabel = "数值 (脉冲 / 脉冲/s / A)";
            break;
        default:  // 全部
            yAxisLabel = "数值 (混合单位)";
            break;
    }
    m_scalarPlot->yAxis->setLabel(yAxisLabel);

    // 按传感器类型分组数据
    QMap<int, QVector<double>> xData;
    QMap<int, QVector<double>> yData;

    for (const auto &window : dataList) {
        double winStartSec = (window.windowStartUs - m_currentRoundStartUs) / 1000000.0;

        // 处理振动数据（仅在选择"全部"或"振动"时显示）
        if (filterType == 0 || filterType == 1) {
            for (auto it = window.vibrationData.begin(); it != window.vibrationData.end(); ++it) {
                int channelId = it.key();
                const QVector<float>& values = it.value();

                if (values.isEmpty()) continue;

                // 计算RMS值作为该窗口的统计指标
                double sumSq = 0.0;
                for (float v : values) {
                    sumSq += v * v;
                }
                double rms = std::sqrt(sumSq / values.size());

                // 使用200+channelId作为sensorType
                int sensorType = 200 + channelId;
                xData[sensorType].append(winStartSec + 0.5);  // 窗口中心点
                yData[sensorType].append(rms);
            }
        }

        // 处理标量数据（MDB和电机）
        for (auto it = window.scalarData.begin(); it != window.scalarData.end(); ++it) {
            int sensorType = it.key();
            const QVector<double>& values = it.value();

            if (values.isEmpty()) continue;

            // 根据筛选类型过滤
            bool include = false;
            switch (filterType) {
                case 0:  // 全部数据
                    include = true;
                    break;
                case 1:  // 振动数据 - 标量数据中不包含振动
                    include = false;
                    break;
                case 2:  // MDB传感器 (100-103)
                    include = (sensorType >= 100 && sensorType <= 103);
                    break;
                case 3:  // 电机参数 (组合键30000-30399 或 原始300-303)
                    include = (sensorType >= 30000 && sensorType < 40000) ||
                              (sensorType >= 300 && sensorType <= 303);
                    break;
            }

            if (!include) continue;

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

        // 图例显示名称+单位
        QString unit = sensorTypeToUnit(type);
        QString legendName = sensorTypeToString(type);
        if (!unit.isEmpty()) {
            legendName += QString(" (%1)").arg(unit);
        }
        m_scalarPlot->graph()->setName(legendName);
        m_scalarPlot->graph()->setPen(QPen(colors[colorIndex % colors.size()], 1.5));
        colorIndex++;
    }

    m_scalarPlot->rescaleAxes();
    m_scalarPlot->replot();
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
    QString dbPath = m_dbPath;

    // 在后台线程执行导出
    QtConcurrent::run([this, roundId, startUs, endUs, filePath, progress, roundStartUs, dbPath]() {
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

        // 写入头部和传感器类型映射说明
        out << "# ================================================\n";
        out << "# DrillControl 数据导出文件\n";
        out << "# ================================================\n";
        out << "# Round ID: " << roundId << "\n";
        out << "# Time Range: " << (startUs - roundStartUs) / 1e6
            << " - " << (endUs - roundStartUs) / 1e6 << " seconds\n";
        out << "# Export Time: " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n";
        out << "#\n";
        out << "# 传感器类型编码说明:\n";
        out << "# 100=上拉力(Force_Upper), 101=下拉力(Force_Lower)\n";
        out << "# 102=扭矩(Torque_MDB), 103=位置(Position_MDB)\n";
        out << "# 200=振动X(Vibration_X), 201=振动Y(Vibration_Y), 202=振动Z(Vibration_Z)\n";
        out << "# 300=电机位置(Motor_Position), 301=电机速度(Motor_Speed)\n";
        out << "# 302=电机扭矩(Motor_Torque), 303=电机电流(Motor_Current)\n";
        out << "# ================================================\n";
        out << "timestamp_sec,sensor_type,sensor_name,value,unit\n";

        // 创建临时查询器
        DataQuerier tempQuerier(dbPath);
        if (!tempQuerier.initialize()) {
            file.close();
            QMetaObject::invokeMethod(progress, "close", Qt::QueuedConnection);
            return;
        }

        auto dataList = tempQuerier.getTimeRangeData(roundId, startUs, endUs);

        for (int i = 0; i < dataList.size(); ++i) {
            const auto& window = dataList[i];

            // 计算相对时间（秒，保留3位小数）
            double relativeSec = (window.windowStartUs - roundStartUs) / 1e6;

            // 写入标量数据（添加传感器名称和单位）
            for (auto it = window.scalarData.begin(); it != window.scalarData.end(); ++it) {
                int sensorType = it.key();
                QString sensorName = sensorTypeToString(sensorType);
                QString unit = sensorTypeToUnit(sensorType);

                for (double value : it.value()) {
                    out << QString::number(relativeSec, 'f', 3) << ","
                        << sensorType << ","
                        << sensorName << ","
                        << value << ","
                        << unit << "\n";
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
// ==================================================
// 亮色主题配置
// ==================================================
void DatabasePage::configureChartDarkTheme(QCustomPlot* plot)
{
    if (!plot) return;

    // 背景和边框
    plot->setBackground(QBrush(QColor(255, 255, 255)));  // 白色背景
    plot->axisRect()->setBackground(QBrush(QColor(250, 250, 250)));  // 浅灰背景

    // 坐标轴样式
    plot->xAxis->setBasePen(QPen(QColor(96, 98, 102)));  // #606266
    plot->yAxis->setBasePen(QPen(QColor(96, 98, 102)));
    plot->xAxis->setTickPen(QPen(QColor(96, 98, 102)));
    plot->yAxis->setTickPen(QPen(QColor(96, 98, 102)));
    plot->xAxis->setSubTickPen(QPen(QColor(144, 147, 153)));  // #909399
    plot->yAxis->setSubTickPen(QPen(QColor(144, 147, 153)));
    plot->xAxis->setTickLabelColor(QColor(48, 49, 51));  // #303133
    plot->yAxis->setTickLabelColor(QColor(48, 49, 51));
    plot->xAxis->setLabelColor(QColor(48, 49, 51));
    plot->yAxis->setLabelColor(QColor(48, 49, 51));

    // 网格线
    plot->xAxis->grid()->setPen(QPen(QColor(220, 223, 230), 1, Qt::DashLine));  // #dcdfe6
    plot->yAxis->grid()->setPen(QPen(QColor(220, 223, 230), 1, Qt::DashLine));
    plot->xAxis->grid()->setSubGridPen(QPen(QColor(235, 238, 245), 1, Qt::DotLine));  // #ebeef5
    plot->yAxis->grid()->setSubGridPen(QPen(QColor(235, 238, 245), 1, Qt::DotLine));
    plot->xAxis->grid()->setSubGridVisible(true);
    plot->yAxis->grid()->setSubGridVisible(true);

    // 图例样式
    plot->legend->setBrush(QBrush(QColor(255, 255, 255, 230)));  // 半透明白色
    plot->legend->setBorderPen(QPen(QColor(220, 223, 230)));  // #dcdfe6
    plot->legend->setTextColor(QColor(48, 49, 51));  // #303133
}

// ==================================================
// 图表点击事件处理
// ==================================================
void DatabasePage::onScalarPlotClicked(QMouseEvent* event)
{
    if (!m_scalarPlot) return;

    double x = m_scalarPlot->xAxis->pixelToCoord(event->pos().x());
    syncTableToChart(x);
}

// ==================================================
// 表格行选中事件
// ==================================================
void DatabasePage::onTableRowSelected()
{
    int row = ui->table_result->currentRow();
    if (row < 0) return;

    syncChartToTable(row);
}

// ==================================================
// 同步：图表 -> 表格
// ==================================================
void DatabasePage::syncTableToChart(double timeInSeconds)
{
    // 在表格中找到最接近的时间行
    int rowCount = ui->table_result->rowCount();
    if (rowCount == 0) return;

    int closestRow = 0;
    double minDiff = std::numeric_limits<double>::max();

    for (int i = 0; i < rowCount; ++i) {
        QTableWidgetItem* item = ui->table_result->item(i, 0);
        if (!item) continue;

        double rowTime = item->text().toDouble();
        double diff = qAbs(rowTime - timeInSeconds);
        if (diff < minDiff) {
            minDiff = diff;
            closestRow = i;
        }
    }

    // 选中并滚动到该行
    ui->table_result->setCurrentCell(closestRow, 0);
    ui->table_result->scrollToItem(ui->table_result->item(closestRow, 0));

    // 更新游标线
    updateChartCursor(timeInSeconds);
}

// ==================================================
// 同步：表格 -> 图表
// ==================================================
void DatabasePage::syncChartToTable(int row)
{
    if (row < 0 || row >= ui->table_result->rowCount()) return;

    QTableWidgetItem* item = ui->table_result->item(row, 0);
    if (!item) return;

    double timeInSeconds = item->text().toDouble();
    updateChartCursor(timeInSeconds);
}

// ==================================================
// 更新游标线位置
// ==================================================
void DatabasePage::updateChartCursor(double timeInSeconds)
{
    if (!m_cursorLine || !m_scalarPlot) return;

    // 转换为图表坐标
    QCPRange xRange = m_scalarPlot->xAxis->range();

    // 如果时间在可见范围内，显示游标线
    if (timeInSeconds >= xRange.lower && timeInSeconds <= xRange.upper) {
        m_cursorLine->start->setCoords(timeInSeconds, 0);
        m_cursorLine->end->setCoords(timeInSeconds, 1);
        m_cursorLine->setVisible(true);
        m_scalarPlot->replot();
    } else {
        m_cursorLine->setVisible(false);
        m_scalarPlot->replot();
    }
}
