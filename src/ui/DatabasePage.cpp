#include "ui/DatabasePage.h"
#include "ui_DatabasePage.h"
#include <QDateTime>
#include <QMessageBox>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QDebug>

DatabasePage::DatabasePage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DatabasePage)
    , m_querier(nullptr)
    , m_currentRoundId(-1)
    , m_currentRoundStartUs(0)
    , m_currentRoundDurationSec(0)
{
    ui->setupUi(this);

    m_querier = new DataQuerier("database/drill_data.db", this);
    if (!m_querier->initialize()) {
        qWarning() << "DataQuerier初始化失败";
    }

    // 连接信号
    connect(ui->btn_refresh_rounds, &QPushButton::clicked, this, &DatabasePage::onRefreshRounds);
    connect(ui->btn_select_round, &QPushButton::clicked, this, &DatabasePage::onRoundSelected);
    connect(ui->table_rounds, &QTableWidget::itemDoubleClicked, this, &DatabasePage::onRoundSelected);
    connect(ui->btn_query, &QPushButton::clicked, this, &DatabasePage::onQuery);
    connect(ui->btn_exec_sql, &QPushButton::clicked, this, &DatabasePage::onExecSql);

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

    int startSec = ui->spin_start_sec->value();
    int endSec = ui->spin_end_sec->value();

    // 计算实际的微秒时间戳
    qint64 startUs = m_currentRoundStartUs + (qint64)startSec * 1000000;
    qint64 endUs = m_currentRoundStartUs + (qint64)endSec * 1000000;

    QList<DataQuerier::WindowData> dataList = m_querier->getTimeRangeData(m_currentRoundId, startUs, endUs);
    displayQueryResult(dataList);
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
