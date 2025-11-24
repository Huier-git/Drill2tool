#include "ui/MainWindow.h"
#include "ui_MainWindow.h"
#include "control/AcquisitionManager.h"
#include <QDebug>
#include <QMessageBox>
#include <QDir>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_acquisitionManager(nullptr)
{
    ui->setupUi(this);

    // 初始化采集系统
    initializeAcquisitionSystem();

    // 设置信号连接
    setupConnections();

    // 设置默认选中第一个页面
    ui->listWidget_pages->setCurrentRow(0);

    qDebug() << "主窗口初始化完成";
}

MainWindow::~MainWindow()
{
    if (m_acquisitionManager) {
        m_acquisitionManager->stopAll();
        delete m_acquisitionManager;
    }
    delete ui;
}

void MainWindow::setupConnections()
{
    // 侧边栏页面切换连接
    connect(ui->listWidget_pages, &QListWidget::currentRowChanged,
            this, &MainWindow::onPageSelectionChanged);
            
    // 菜单栏动作连接
    connect(ui->action_exit, &QAction::triggered, this, &QWidget::close);
    connect(ui->action_about, &QAction::triggered, this, [this](){
        statusBar()->showMessage("钻机采集控制系统 v2.0 - KT团队", 3000);
    });
}

void MainWindow::onPageSelectionChanged(int currentRow)
{
    // 切换到对应页面
    ui->stackedWidget_pages->setCurrentIndex(currentRow);
    
    // 状态栏提示
    QString pageName;
    switch(currentRow) {
        case 0: pageName = "数据采集"; break;
        case 1: pageName = "钻机控制"; break;
        case 2: pageName = "数据库管理"; break;
        default: pageName = "未知页面"; break;
    }
    
    statusBar()->showMessage(QString("切换到: %1").arg(pageName), 2000);
    qDebug() << "页面切换至：" << currentRow << pageName;
}

void MainWindow::initializeAcquisitionSystem()
{
    // 创建采集管理器
    m_acquisitionManager = new AcquisitionManager(this);

    // 设置数据库路径
    QString dbPath = QDir::currentPath() + "/database/drill_data.db";
    QDir().mkpath(QDir::currentPath() + "/database");  // 确保目录存在

    // 初始化数据库
    if (!m_acquisitionManager->initialize(dbPath)) {
        QMessageBox::critical(this, "初始化失败",
            "无法初始化数据采集系统，请检查数据库配置！");
        qCritical() << "采集系统初始化失败！";
        return;
    }

    // 将采集管理器传递给SensorPage
    ui->sensorPage->setAcquisitionManager(m_acquisitionManager);

    qDebug() << "采集系统初始化成功，数据库路径：" << dbPath;
}
