#include "ui/ControlPage.h"
#include "ui_ControlPage.h"
#include "Global.h"
#include "control/zmotion.h"
#include "control/zmcaux.h"
#include <QDebug>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QThread>

#define ERR_OK 0
#define TIMER_BASIC_INFO_INTERVAL 500
#define TIMER_ADVANCE_INFO_INTERVAL 500
#define TIMER_REALTIME_PARM_INTERVAL 100
#define MAX_MOTOR_COUNT 10  // MotorMap数组大小上限

ControlPage::ControlPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ControlPage)
    , m_basicInfoTimer(nullptr)
    , m_advanceInfoTimer(nullptr)
    , m_realtimeParmTimer(nullptr)
    , m_initFlag(false)
    , m_axisNum(0)
    , m_initStatus(0)
    , m_nodeNum(0)
    , m_oldRow(-1)
    , m_oldCol(-1)
{
    ui->setupUi(this);

    // 初始化定时器
    m_basicInfoTimer = new QTimer(this);
    m_advanceInfoTimer = new QTimer(this);
    m_realtimeParmTimer = new QTimer(this);

    m_basicInfoTimer->setInterval(TIMER_BASIC_INFO_INTERVAL);
    m_advanceInfoTimer->setInterval(TIMER_ADVANCE_INFO_INTERVAL);
    m_realtimeParmTimer->setInterval(TIMER_REALTIME_PARM_INTERVAL);

    // 设置按钮样式类型
    ui->btn_stop_all_motors->setProperty("type", "emergency");  // 急停按钮
    ui->btn_enable->setProperty("type", "success");  // 使能按钮
    ui->btn_bus_init->setProperty("type", "primary");  // 主要操作
    ui->btn_motor_parm_update->setProperty("type", "primary");  // 主要操作

    // 刷新样式
    ui->btn_stop_all_motors->style()->unpolish(ui->btn_stop_all_motors);
    ui->btn_stop_all_motors->style()->polish(ui->btn_stop_all_motors);
    ui->btn_enable->style()->unpolish(ui->btn_enable);
    ui->btn_enable->style()->polish(ui->btn_enable);
    ui->btn_bus_init->style()->unpolish(ui->btn_bus_init);
    ui->btn_bus_init->style()->polish(ui->btn_bus_init);
    ui->btn_motor_parm_update->style()->unpolish(ui->btn_motor_parm_update);
    ui->btn_motor_parm_update->style()->polish(ui->btn_motor_parm_update);

    // 连接定时器
    connect(m_basicInfoTimer, &QTimer::timeout, this, &ControlPage::basicInfoRefresh);
    connect(m_realtimeParmTimer, &QTimer::timeout, this, &ControlPage::refreshTableContent);
    connect(m_advanceInfoTimer, &QTimer::timeout, this, &ControlPage::advanceInfoRefresh);

    setupConnections();

    // 禁用编辑模式下的更新按钮
    ui->btn_motor_parm_update->setEnabled(true);
}

ControlPage::~ControlPage()
{
    if (m_basicInfoTimer) m_basicInfoTimer->stop();
    if (m_advanceInfoTimer) m_advanceInfoTimer->stop();
    if (m_realtimeParmTimer) m_realtimeParmTimer->stop();
    delete ui;
}

void ControlPage::setupConnections()
{
    // 总线控制
    connect(ui->btn_bus_init, &QPushButton::clicked, this, &ControlPage::onBusInitClicked);
    connect(ui->btn_stop_all_motors, &QPushButton::clicked, this, &ControlPage::onStopAllMotorsClicked);

    // 电机表格
    connect(ui->btn_motor_parm_update, &QPushButton::clicked, this, &ControlPage::onMotorParmUpdateClicked);
    connect(ui->cb_motor_parm_edit, &QCheckBox::stateChanged, this, &ControlPage::onMotorParmEditChanged);
    connect(ui->cb_motor_rt_refresh, &QCheckBox::stateChanged, this, &ControlPage::onMotorRtRefreshChanged);

    // 轴信息
    connect(ui->cb_auto_update, &QCheckBox::stateChanged, this, &ControlPage::onAutoUpdateChanged);
    connect(ui->btn_update, &QPushButton::clicked, this, &ControlPage::onUpdateClicked);
    connect(ui->btn_enable, &QPushButton::clicked, this, &ControlPage::onEnableClicked);
    connect(ui->btn_clear_alarm, &QPushButton::clicked, this, &ControlPage::onClearAlarmClicked);
    connect(ui->btn_set_zero, &QPushButton::clicked, this, &ControlPage::onSetZeroClicked);

    // 命令
    connect(ui->btn_send_cmd, &QPushButton::clicked, this, &ControlPage::onSendCmdClicked);
}

// ============================================================================
// 总线控制
// ============================================================================

void ControlPage::onBusInitClicked()
{
    if (g_handle == nullptr) {
        QMessageBox::warning(this, "错误", "请先在传感器页面连接ZMotion控制器");
        ui->tb_cmd_window->append("错误：未连接控制器");
        return;
    }

    char cmdbuffAck[2048];
    int ret = ZAux_Execute(g_handle, "RUNTASK 1,ECAT_Init", cmdbuffAck, 2048);

    ui->tb_cmd_window->append(toCmdWindow(cmdbuffAck));

    if (ret != ERR_OK) {
        ui->le_bus_init_status->setText("初始化失败");
        ui->le_bus_init_status->setStyleSheet("border: 1px solid #ccc; padding: 5px; background-color: #ffcccc;");
        ui->tb_cmd_window->append("错误：总线初始化失败");
        qDebug() << "[ControlPage] 总线初始化失败，错误代码:" << ret;
    } else {
        ui->le_bus_init_status->setText("初始化成功");
        ui->le_bus_init_status->setStyleSheet("border: 1px solid #ccc; padding: 5px; background-color: #ccffcc;");
        ui->tb_cmd_window->append("总线初始化成功");
        m_initFlag = true;
        qDebug() << "[ControlPage] 总线初始化成功";

        // 启动定时器
        if (m_basicInfoTimer && !m_basicInfoTimer->isActive()) {
            m_basicInfoTimer->start();
        }
    }
}

void ControlPage::onStopAllMotorsClicked()
{
    if (g_handle == nullptr) {
        QMessageBox::warning(this, "错误", "未连接控制器");
        return;
    }

    // 使用实际初始化的轴数，上限MAX_MOTOR_COUNT（MotorMap数组大小）
    int n = (m_axisNum > 0) ? qMin(static_cast<int>(m_axisNum), MAX_MOTOR_COUNT) : MAX_MOTOR_COUNT;
    for (int i = 0; i < n; ++i) {
        ZAux_Direct_Single_Cancel(g_handle, MotorMap[i], 0);
    }

    ui->tb_cmd_window->append("所有电机已停止");
    qDebug() << "[ControlPage] 所有电机已停止";
}

// ============================================================================
// 电机表格
// ============================================================================

void ControlPage::onMotorParmUpdateClicked()
{
    initMotorTable();
    refreshTableContent();
}

void ControlPage::onMotorParmEditChanged(int state)
{
    if (state == Qt::Checked) {
        ui->btn_motor_parm_update->setEnabled(false);
        connect(ui->tb_motor, &QTableWidget::cellDoubleClicked, this, &ControlPage::unmodifyMotorTable);
        connect(ui->tb_motor, &QTableWidget::itemChanged, this, &ControlPage::modifyMotorTable);
    } else {
        ui->btn_motor_parm_update->setEnabled(true);
        disconnect(ui->tb_motor, &QTableWidget::cellDoubleClicked, this, &ControlPage::unmodifyMotorTable);
        disconnect(ui->tb_motor, &QTableWidget::itemChanged, this, &ControlPage::modifyMotorTable);
        initMotorTable();
        refreshTableContent();
    }
}

void ControlPage::onMotorRtRefreshChanged(int state)
{
    if (state == Qt::Checked) {
        if (!m_realtimeParmTimer->isActive()) {
            m_realtimeParmTimer->start();
        }
    } else {
        if (m_realtimeParmTimer->isActive()) {
            m_realtimeParmTimer->stop();
        }
    }
}

// ============================================================================
// 轴信息
// ============================================================================

void ControlPage::onAutoUpdateChanged(int state)
{
    if (state == Qt::Checked) {
        if (!m_advanceInfoTimer->isActive()) {
            m_advanceInfoTimer->start();
        }
    } else {
        if (m_advanceInfoTimer->isActive()) {
            m_advanceInfoTimer->stop();
        }
    }
}

void ControlPage::onUpdateClicked()
{
    advanceInfoRefresh();
}

// ============================================================================
// 命令
// ============================================================================

void ControlPage::onSendCmdClicked()
{
    if (g_handle == nullptr) {
        ui->tb_cmd_window->append("错误：未连接控制器");
        return;
    }

    QString cmd = ui->tx_cmd->text().trimmed();
    if (cmd.isEmpty()) {
        return;
    }

    // 特殊命令：显示电机映射
    if (cmd == "?Map") {
        int n = (m_axisNum > 0) ? qMin(static_cast<int>(m_axisNum), MAX_MOTOR_COUNT) : MAX_MOTOR_COUNT;
        QString mapInfo = QString("[电机映射] 当前映射 (共%1轴):\n").arg(n);
        for (int i = 0; i < n; ++i) {
            mapInfo += QString("M%1 -> %2\n").arg(i).arg(MotorMap[i]);
        }
        ui->tb_cmd_window->append(mapInfo);
        ui->tx_cmd->clear();
        return;
    }

    // 发送命令到ZMotion控制器
    char cmdbuffAck[2048];
    QByteArray cmdBytes = cmd.toLocal8Bit();
    int ret = ZAux_DirectCommand(g_handle, cmdBytes.data(), cmdbuffAck, 2048);

    if (ret == ERR_OK) {
        ui->tb_cmd_window->append(QString("> %1").arg(cmd));
        ui->tb_cmd_window->append(toCmdWindow(cmdbuffAck));
    } else {
        ui->tb_cmd_window->append(QString("错误: 命令执行失败 (错误代码: %1)").arg(ret));
    }

    ui->tx_cmd->clear();
}

// ============================================================================
// 电机表格初始化和刷新
// ============================================================================

void ControlPage::initMotorTable()
{
    if (g_handle == nullptr || !m_initFlag) {
        qDebug() << "[ControlPage] 无法获取电机参数：未连接或未初始化";
        return;
    }

    // 使用实际初始化的轴数，若未获取则默认MAX_MOTOR_COUNT，上限MAX_MOTOR_COUNT（MotorMap数组大小）
    int n = (m_axisNum > 0) ? qMin(static_cast<int>(m_axisNum), MAX_MOTOR_COUNT) : MAX_MOTOR_COUNT;
    ui->tb_motor->setRowCount(n);

    // 设置行头标签（电机名称）
    QStringList verticalHeaderLabels;
    verticalHeaderLabels << "回转" << "冲击" << "进给" << "下夹紧"
                        << "机械手夹紧" << "机械手回转" << "机械手伸缩" << "存储机构" << "M8" << "M9";
    // 如果轴数超过预设名称，补充默认名称
    while (verticalHeaderLabels.size() < n) {
        verticalHeaderLabels << QString("M%1").arg(verticalHeaderLabels.size());
    }
    ui->tb_motor->setVerticalHeaderLabels(verticalHeaderLabels);

    // 设置列宽
    ui->tb_motor->setColumnWidth(0, 50);   // EN
    ui->tb_motor->setColumnWidth(1, 100);  // MPos
    ui->tb_motor->setColumnWidth(2, 100);  // Pos
    ui->tb_motor->setColumnWidth(3, 100);  // MVel
    ui->tb_motor->setColumnWidth(4, 100);  // Vel
    ui->tb_motor->setColumnWidth(5, 50);   // DAC
    ui->tb_motor->setColumnWidth(6, 50);   // Atype
    ui->tb_motor->setColumnWidth(7, 50);   // Unit
    ui->tb_motor->setColumnWidth(8, 100);  // Acc
    ui->tb_motor->setColumnWidth(9, 100);  // Dec

    // 初始化每个单元格
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < 10; ++j) {
            QTableWidgetItem *item = new QTableWidgetItem("");
            ui->tb_motor->setItem(i, j, item);
        }
    }
}

void ControlPage::refreshTableContent()
{
    if (g_handle == nullptr || !m_initFlag) {
        return;
    }

    int iEN, iAType;
    float fMPos, fDPos, fMVel, fDVel, fDAC, fUnit, fAcc, fDec;

    // 使用实际初始化的轴数，上限MAX_MOTOR_COUNT（MotorMap数组大小）
    int n = (m_axisNum > 0) ? qMin(static_cast<int>(m_axisNum), MAX_MOTOR_COUNT) : qMin(ui->tb_motor->rowCount(), MAX_MOTOR_COUNT);

    // 读取电机的参数
    for (int i = 0; i < n; ++i) {
        int ret = 0;
        ret += ZAux_Direct_GetAtype(g_handle, MotorMap[i], &iAType);
        ret += ZAux_Direct_GetAxisEnable(g_handle, MotorMap[i], &iEN);
        ret += ZAux_Direct_GetDpos(g_handle, MotorMap[i], &fDPos);
        ret += ZAux_Direct_GetMpos(g_handle, MotorMap[i], &fMPos);
        ret += ZAux_Direct_GetSpeed(g_handle, MotorMap[i], &fDVel);
        ret += ZAux_Direct_GetMspeed(g_handle, MotorMap[i], &fMVel);
        ret += ZAux_Direct_GetUnits(g_handle, MotorMap[i], &fUnit);
        ret += ZAux_Direct_GetAccel(g_handle, MotorMap[i], &fAcc);
        ret += ZAux_Direct_GetDecel(g_handle, MotorMap[i], &fDec);

        if (iAType == 65) {  // 位置模式下的DAC是0
            fDAC = 0;
        } else {
            ZAux_Direct_GetDAC(g_handle, MotorMap[i], &fDAC);
        }

        if (ret != ERR_OK) {
            continue;  // 跳过读取失败的电机
        }

        // 更新表格
        ui->tb_motor->item(i, 0)->setText(QString::number(iEN));
        ui->tb_motor->item(i, 1)->setText(QString::number(fMPos, 'f', 2));
        ui->tb_motor->item(i, 2)->setText(QString::number(fDPos, 'f', 2));
        ui->tb_motor->item(i, 3)->setText(QString::number(fMVel, 'f', 2));
        ui->tb_motor->item(i, 4)->setText(QString::number(fDVel, 'f', 2));
        ui->tb_motor->item(i, 5)->setText(QString::number(fDAC, 'f', 2));
        ui->tb_motor->item(i, 6)->setText(QString::number(iAType));
        ui->tb_motor->item(i, 7)->setText(QString::number(fUnit, 'f', 0));
        ui->tb_motor->item(i, 8)->setText(QString::number(fAcc, 'f', 2));
        ui->tb_motor->item(i, 9)->setText(QString::number(fDec, 'f', 2));
    }
}

void ControlPage::advanceInfoRefresh()
{
    if (g_handle == nullptr || !m_initFlag) {
        return;
    }

    int selectindex = ui->cb_axis_num->currentIndex();
    if (selectindex == -1) {
        ui->tb_cmd_window->append("错误：轴号索引无效");
        return;
    }

    // 刷新轴号下拉框
    if (ui->cb_axis_num->count() != static_cast<int>(m_axisNum)) {
        ui->cb_axis_num->clear();
        for (int i = 0; i < static_cast<int>(m_axisNum); ++i) {
            ui->cb_axis_num->addItem(QString::number(i));
        }
    }

    // 读取选中轴的参数
    int m_atype, m_AxisStatus, m_Idle, m_bAxisEnable;
    float m_units, m_speed, m_accel, m_decel, m_fMpos, m_fDpos;

    ZAux_Direct_GetAtype(g_handle, MotorMap[selectindex], &m_atype);
    ZAux_Direct_GetUnits(g_handle, MotorMap[selectindex], &m_units);
    ZAux_Direct_GetSpeed(g_handle, MotorMap[selectindex], &m_speed);
    ZAux_Direct_GetAccel(g_handle, MotorMap[selectindex], &m_accel);
    ZAux_Direct_GetDecel(g_handle, MotorMap[selectindex], &m_decel);

    ui->le_atype->setText(QString::number(m_atype));
    ui->le_pulse_equivalent->setText(QString::number(m_units));
    ui->le_speed->setText(QString::number(m_speed));
    ui->le_accel->setText(QString::number(m_accel));
    ui->le_decel->setText(QString::number(m_decel));

    ZAux_Direct_GetMpos(g_handle, MotorMap[selectindex], &m_fMpos);
    ZAux_Direct_GetDpos(g_handle, MotorMap[selectindex], &m_fDpos);
    ZAux_Direct_GetAxisStatus(g_handle, MotorMap[selectindex], &m_AxisStatus);
    ZAux_Direct_GetIfIdle(g_handle, MotorMap[selectindex], &m_Idle);

    ui->le_direct_axis_pos->setText(QString::number(m_fDpos));
    ui->le_current_axis_pos->setText(QString::number(m_fMpos));
    ui->le_axis_status->setText(QString::number(m_AxisStatus));
    ui->le_if_idle->setText(m_Idle == 0 ? "运动中" : (m_Idle == -1 ? "完成" : ""));

    ZAux_Direct_GetAxisEnable(g_handle, MotorMap[selectindex], &m_bAxisEnable);
    ui->btn_enable->setText(m_bAxisEnable ? "禁用" : "使能");
    ui->le_enable_status->setText(m_bAxisEnable ? "开启" : "关闭");
    ui->btn_enable->setChecked(m_bAxisEnable);
}

void ControlPage::basicInfoRefresh()
{
    if (g_handle == nullptr) {
        return;
    }

    int ret;
    char Bus_InitStatus[] = "ECAT_InitEnable";
    char Bus_TotalAxisnum[] = "BusAxis_Num";

    ret = ZAux_Direct_GetUserVar(g_handle, Bus_InitStatus, &m_initStatus);
    ret += ZAux_BusCmd_GetNodeNum(g_handle, 0, &m_nodeNum);
    ret += ZAux_Direct_GetUserVar(g_handle, Bus_TotalAxisnum, &m_axisNum);

    if (ret != ERR_OK) {
        ui->le_bus_init_status->setText("初始化失败");
        ui->le_bus_init_status->setStyleSheet("border: 1px solid #ccc; padding: 5px; background-color: #ffcccc;");
        qDebug() << "[ControlPage] 无法读取总线变量";
        return;
    }

    ui->le_bus_init_status->setText("初始化完成");
    ui->le_bus_init_status->setStyleSheet("border: 1px solid #ccc; padding: 5px; background-color: #ccffcc;");
    m_initFlag = true;
    ui->le_bus_node_num->setText(QString::number(m_nodeNum));
    ui->le_total_axis_num->setText(QString::number(static_cast<int>(m_axisNum)));
}

void ControlPage::onEnableClicked()
{
    if (g_handle == nullptr) {
        QMessageBox::warning(this, "错误", "未连接控制器");
        return;
    }

    int selectindex = ui->cb_axis_num->currentIndex();
    if (selectindex == -1) {
        QMessageBox::warning(this, "错误", "请先选择轴号");
        return;
    }

    int motorID = MotorMap[selectindex];
    int currentEnable;
    ZAux_Direct_GetAxisEnable(g_handle, motorID, &currentEnable);

    // 切换使能状态
    int newEnable = currentEnable ? 0 : 1;
    int ret = ZAux_Direct_SetAxisEnable(g_handle, motorID, newEnable);

    if (ret == ERR_OK) {
        ui->tb_cmd_window->append(QString("轴%1 %2成功").arg(selectindex).arg(newEnable ? "使能" : "禁用"));
        // 刷新显示
        advanceInfoRefresh();
    } else {
        ui->tb_cmd_window->append(QString("错误：轴%1 %2失败").arg(selectindex).arg(newEnable ? "使能" : "禁用"));
    }
}

void ControlPage::onClearAlarmClicked()
{
    if (g_handle == nullptr) {
        QMessageBox::warning(this, "错误", "未连接控制器");
        return;
    }

    int selectindex = ui->cb_axis_num->currentIndex();
    if (selectindex == -1) {
        QMessageBox::warning(this, "错误", "请先选择轴号");
        return;
    }

    int motorID = MotorMap[selectindex];
    int ret = ZAux_BusCmd_DriveClear(g_handle, motorID, 0);

    if (ret == ERR_OK) {
        ui->tb_cmd_window->append(QString("✓ 轴%1 报警已清除").arg(selectindex));
        qDebug() << "[ControlPage] 轴" << selectindex << "报警已清除";
    } else {
        ui->tb_cmd_window->append(QString("✗ 错误：轴%1 清除报警失败 (错误码: %2)").arg(selectindex).arg(ret));
        qDebug() << "[ControlPage] 轴" << selectindex << "清除报警失败，错误码:" << ret;
    }
}

void ControlPage::onSetZeroClicked()
{
    if (g_handle == nullptr) {
        QMessageBox::warning(this, "错误", "未连接控制器");
        return;
    }

    int selectindex = ui->cb_axis_num->currentIndex();
    if (selectindex == -1) {
        QMessageBox::warning(this, "错误", "请先选择轴号");
        return;
    }

    int motorID = MotorMap[selectindex];
    int ret = ZAux_Direct_SetMpos(g_handle, motorID, 0);

    if (ret == ERR_OK) {
        ui->tb_cmd_window->append(QString("✓ 轴%1 已设置为零点").arg(selectindex));
        qDebug() << "[ControlPage] 轴" << selectindex << "已设置为零点";
        // 刷新显示
        advanceInfoRefresh();
    } else {
        ui->tb_cmd_window->append(QString("✗ 错误：轴%1 设置零点失败 (错误码: %2)").arg(selectindex).arg(ret));
        qDebug() << "[ControlPage] 轴" << selectindex << "设置零点失败，错误码:" << ret;
    }
}

void ControlPage::unmodifyMotorTable(int row, int column)
{
    // 保存双击单元格时的旧值，用于后续恢复
    if (ui->tb_motor->item(row, column)) {
        m_oldCellValue = ui->tb_motor->item(row, column)->text();
        m_oldRow = row;
        m_oldCol = column;
        qDebug() << "[ControlPage] 选中单元格 行:" << row << " 列:" << column << " 值:" << m_oldCellValue;
    }
}

void ControlPage::modifyMotorTable(QTableWidgetItem *item)
{
    if (g_handle == nullptr || !item) {
        return;
    }

    int row = item->row();
    int col = item->column();

    // 忽略MPos(列1)和MVel(列3)的编辑 - 这些是只读反馈值
    if (col == 1 || col == 3) {
        qDebug() << "[ControlPage] 列" << col << "是只读列，忽略编辑";
        return;
    }

    QString newCellValue = item->text();

    // 如果新内容与旧内容相同，防止重复触发
    if (newCellValue == m_oldCellValue) {
        return;
    }

    // 数值验证
    if (!isNumeric(newCellValue)) {
        qDebug() << "[ControlPage] 无效的数值:" << newCellValue;
        item->setText(m_oldCellValue);  // 恢复旧值
        ui->tb_cmd_window->append(QString("错误：'%1' 不是有效的数值，已恢复原值").arg(newCellValue));
        return;
    }

    int motorID = MotorMap[row];
    float value = newCellValue.toFloat();
    int ret = 1;

    // 根据列号设置不同的参数
    switch (col) {
        case 0:  // EN - 使能
            ret = ZAux_Direct_SetAxisEnable(g_handle, motorID, static_cast<int>(value));
            break;

        case 2: {  // Pos - 位置（需要触发运动）
            // 1. 先取消当前运动
            ZAux_Direct_Single_Cancel(g_handle, motorID, 0);
            QThread::msleep(10);  // 短暂延时确保取消完成

            // 2. 根据UI选择使用绝对或相对运动
            if (ui->cb_motor_pos_abs->isChecked()) {
                // 绝对运动模式
                ret = ZAux_Direct_Single_MoveAbs(g_handle, motorID, value);
                qDebug() << "[ControlPage] 触发绝对运动到位置:" << value;
            } else {
                // 相对运动模式
                ret = ZAux_Direct_Single_Move(g_handle, motorID, value);
                qDebug() << "[ControlPage] 触发相对运动，距离:" << value;
            }
            break;
        }

        case 4:  // Vel - 速度
            ret = ZAux_Direct_SetSpeed(g_handle, motorID, value);
            break;

        case 5: {  // DAC - 输出（需要检查轴类型）
            int iAType;
            ZAux_Direct_GetAtype(g_handle, motorID, &iAType);

            // 只有力矩模式(66)或速度模式(67)才能设置DAC
            if (iAType == 66 || iAType == 67) {
                ret = ZAux_Direct_SetDAC(g_handle, motorID, value);
            } else {
                ret = ZAux_Direct_SetDAC(g_handle, motorID, 0);
                ui->tb_cmd_window->append(QString("警告：电机%1的轴类型为%2，不是力矩/速度模式，DAC已设为0").arg(row).arg(iAType));
            }
            break;
        }

        case 6:  // Atype - 轴类型
            ret = ZAux_Direct_SetAtype(g_handle, motorID, static_cast<int>(value));
            break;

        case 7:  // Unit - 脉冲当量
            ret = ZAux_Direct_SetUnits(g_handle, motorID, value);
            break;

        case 8:  // Acc - 加速度
            ret = ZAux_Direct_SetAccel(g_handle, motorID, value);
            break;

        case 9:  // Dec - 减速度
            ret = ZAux_Direct_SetDecel(g_handle, motorID, value);
            break;

        default:
            break;
    }

    // 操作结果反馈
    if (ret == ERR_OK) {
        qDebug() << "[ControlPage] 成功修改 行:" << row << " 列:" << col << " 新值:" << value << " 电机:" << motorID;
        ui->tb_cmd_window->append(QString("✓ 电机%1参数已更新").arg(row));
    } else {
        qDebug() << "[ControlPage] 修改失败 行:" << row << " 列:" << col << " 值:" << value << " 错误码:" << ret;
        ui->tb_cmd_window->append(QString("✗ 错误：电机%1参数更新失败 (错误码: %2)").arg(row).arg(ret));
        // 失败时恢复旧值
        item->setText(m_oldCellValue);
    }
}

bool ControlPage::isNumeric(const QString &str)
{
    bool ok;
    str.toFloat(&ok);
    return ok;
}

QString ControlPage::toCmdWindow(const char* response)
{
    if (response == nullptr) {
        return "";
    }

    QString str = QString::fromLocal8Bit(response);
    str.replace("\r\n", "\n");
    str.replace("\r", "\n");

    return str;
}
