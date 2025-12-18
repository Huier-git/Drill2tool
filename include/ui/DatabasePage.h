#ifndef DATABASEPAGE_H
#define DATABASEPAGE_H

#include <QWidget>
#include "database/DataQuerier.h"

QT_BEGIN_NAMESPACE
namespace Ui { class DatabasePage; }
QT_END_NAMESPACE

class DatabasePage : public QWidget
{
    Q_OBJECT

public:
    explicit DatabasePage(QWidget *parent = nullptr);
    ~DatabasePage();

private slots:
    void onRefreshRounds();
    void onRoundSelected();
    void onQuery();
    void onExecSql();

    // 快捷选择
    void onSelectAll();
    void onSelectFirst10();
    void onSelectLast10();

    // 时间范围变化
    void onStartSecChanged(int value);
    void onEndSecChanged(int value);

private:
    void loadRoundsList();
    void updateRoundInfo(int roundId, qint64 durationSec);
    void displayQueryResult(const QList<DataQuerier::WindowData> &dataList);

private:
    Ui::DatabasePage *ui;
    DataQuerier *m_querier;

    // 当前选中轮次信息
    int m_currentRoundId;
    qint64 m_currentRoundStartUs;
    qint64 m_currentRoundDurationSec;
};

#endif // DATABASEPAGE_H
