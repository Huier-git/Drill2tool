#ifndef CONTROLPAGE_H
#define CONTROLPAGE_H

#include <QWidget>
#include <QTimer>
#include <QMap>

#include "control/UnitConverter.h"

QT_BEGIN_NAMESPACE
namespace Ui { class ControlPage; }
QT_END_NAMESPACE

class QTableWidgetItem;

/**
 * @brief 运动控制页面（重构自原项目zmotionpage第一页）
 *
 * 功能：
 * 1. 总线初始化
 * 2. 电机参数表格显示（EN, MPos, Pos, MVel, Vel, DAC, Atype, Unit, Acc, Dec）
 * 3. 命令窗口（发送命令到ZMotion控制器）
 * 4. 自动更新电机参数
 * 5. 停止所有电机
 *
 * 注意：连接功能已移至SensorPage，使用全局g_handle
 */
class ControlPage : public QWidget
{
    Q_OBJECT

public:
    explicit ControlPage(QWidget *parent = nullptr);
    ~ControlPage();

private slots:
    // 总线控制
    void onBusInitClicked();
    void onStopAllMotorsClicked();

    // 电机表格
    void onMotorParmUpdateClicked();
    void onMotorParmEditChanged(int state);
    void onMotorRtRefreshChanged(int state);
    void onUnitModeChanged(int state);

    // 轴信息
    void onAutoUpdateChanged(int state);
    void onUpdateClicked();
    void onEnableClicked();
    void onClearAlarmClicked();
    void onSetZeroClicked();

    // 命令
    void onSendCmdClicked();

    // 定时器
    void refreshTableContent();
    void advanceInfoRefresh();
    void basicInfoRefresh();
    void onMotorTableSelectionChanged();

private:
    void setupConnections();
    void refreshUnitConfig();
    AxisUnitInfo axisUnitInfo(int axisIndex) const;
    int currentAxisIndex() const;
    double displayValueFromDriver(double driverValue, int axisIndex, UnitValueType type) const;
    double driverValueFromDisplay(double displayValue, int axisIndex, UnitValueType type) const;
    void updateUnitsStatus(int axisIndex = -1);
    QString unitLabelForAxis(int axisIndex) const;
    QString configDirPath() const;
    void initMotorTable();
    void modifyMotorTable(QTableWidgetItem *item);
    void unmodifyMotorTable(int row, int column);
    QString toCmdWindow(const char* response);
    bool isNumeric(const QString &str);  // 数值验证

private:
    Ui::ControlPage *ui;

    // 定时器
    QTimer *m_basicInfoTimer;
    QTimer *m_advanceInfoTimer;
    QTimer *m_realtimeParmTimer;

    // 状态
    bool m_initFlag;
    float m_axisNum;

    // 总线信息
    float m_initStatus;
    int m_nodeNum;

    // 表格编辑
    QString m_oldCellValue;  // 保存旧值用于恢复
    int m_oldRow;
    int m_oldCol;

    bool m_displayPhysicalUnits;
    bool m_tableSyncing;
    QMap<int, AxisUnitInfo> m_axisUnits;
};

#endif // CONTROLPAGE_H
