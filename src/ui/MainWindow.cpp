#include "ui/MainWindow.h"
#include "ui_MainWindow.h"
#include "ui/VibrationPage.h"
#include "ui/MdbPage.h"
#include "ui/SensorPage.h"
#include "ui/MotorPage.h"
#include "ui/ControlPage.h"
#include "ui/DatabasePage.h"
#include "ui/DrillControlPage.h"
#include "ui/PlanVisualizerPage.h"
#include "control/AcquisitionManager.h"
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QDebug>

// ============================================
// DetachedWindow 实现
// ============================================
DetachedWindow::DetachedWindow(int pageIndex, QWidget *page, const QString &title, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_pageIndex(pageIndex)
    , m_page(page)
{
    setWindowTitle(title);
    resize(900, 650);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(page);
    page->show();
}

DetachedWindow::~DetachedWindow()
{
}

QWidget* DetachedWindow::takeWidget()
{
    if (m_page) {
        layout()->removeWidget(m_page);
        m_page->setParent(nullptr);
    }
    return m_page;
}

void DetachedWindow::closeEvent(QCloseEvent *event)
{
    emit windowClosed(m_pageIndex);
    event->accept();
}

// ============================================
// MainWindow 实现
// ============================================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_acquisitionManager(nullptr)
    , m_sensorPage(nullptr)
    , m_vibrationPage(nullptr)
    , m_mdbPage(nullptr)
    , m_motorPage(nullptr)
    , m_controlPage(nullptr)
    , m_databasePage(nullptr)
    , m_drillControlPage(nullptr)
    , m_planVisualizerPage(nullptr)
    , m_detachButton(nullptr)
{
    ui->setupUi(this);
    setupAcquisitionManager();
    setupPages();
    setupDetachButton();
    setupConnections();
}

MainWindow::~MainWindow()
{
    // 关闭所有弹出窗口
    for (auto *win : m_detachedWindows) {
        win->close();
        delete win;
    }
    delete ui;
}

void MainWindow::setupAcquisitionManager()
{
    m_acquisitionManager = new AcquisitionManager(this);
    m_acquisitionManager->initialize();
}

void MainWindow::setupPages()
{
    // 页面名称
    m_pageNames << "数据采集" << "振动监测" << "Modbus监测" << "数据库管理" << "电机参数" << "运动控制" << "钻机高级控制" << "钻杆规划";

    // 获取UI中自动创建的页面
    m_sensorPage = ui->sensorPage;
    m_vibrationPage = ui->vibrationPage;
    m_mdbPage = ui->mdbPage;
    m_databasePage = ui->databasePage;

    // 手动创建 MotorPage
    m_motorPage = new MotorPage(this);
    ui->stackedWidget_pages->addWidget(m_motorPage);
    ui->listWidget_pages->addItem("电机参数");

    // 手动创建 ControlPage
    m_controlPage = new ControlPage(this);
    ui->stackedWidget_pages->addWidget(m_controlPage);
    ui->listWidget_pages->addItem("运动控制");

    // 手动创建 DrillControlPage
    m_drillControlPage = new DrillControlPage(this);
    ui->stackedWidget_pages->addWidget(m_drillControlPage);
    ui->listWidget_pages->addItem("钻机高级控制");

    // 手动创建 PlanVisualizerPage
    m_planVisualizerPage = new PlanVisualizerPage(this);
    ui->stackedWidget_pages->addWidget(m_planVisualizerPage);
    ui->listWidget_pages->addItem("钻杆规划");

    // 记录容器页面（StackedWidget的直接子控件）
    m_pageWidgets[0] = ui->stackedWidget_pages->widget(0);  // page_dataCollection
    m_pageWidgets[1] = ui->stackedWidget_pages->widget(1);  // page_vibration
    m_pageWidgets[2] = ui->stackedWidget_pages->widget(2);  // page_mdb
    m_pageWidgets[3] = ui->stackedWidget_pages->widget(3);  // page_database
    m_pageWidgets[4] = m_motorPage;
    m_pageWidgets[5] = m_controlPage;
    m_pageWidgets[6] = m_drillControlPage;
    m_pageWidgets[7] = m_planVisualizerPage;

    // 设置 AcquisitionManager
    if (m_sensorPage) m_sensorPage->setAcquisitionManager(m_acquisitionManager);
    if (m_vibrationPage) m_vibrationPage->setAcquisitionManager(m_acquisitionManager);
    if (m_mdbPage) m_mdbPage->setAcquisitionManager(m_acquisitionManager);
    if (m_motorPage) m_motorPage->setAcquisitionManager(m_acquisitionManager);

    ui->listWidget_pages->setCurrentRow(0);
}

void MainWindow::setupConnections()
{
    connect(ui->listWidget_pages, &QListWidget::currentRowChanged,
            this, &MainWindow::onPageSelectionChanged);

    // 双击弹出独立窗口
    connect(ui->listWidget_pages, &QListWidget::itemDoubleClicked,
            this, &MainWindow::onPageDoubleClicked);
}

void MainWindow::setupDetachButton()
{
    m_detachButton = new QPushButton(ui->stackedWidget_pages);
    m_detachButton->setText("⇗");
    m_detachButton->setFixedSize(32, 32);
    m_detachButton->setToolTip("弹出为独立窗口 (双击侧边栏也可以)");
    m_detachButton->setCursor(Qt::PointingHandCursor);
    m_detachButton->setStyleSheet(
        "QPushButton {"
        "  background-color: rgba(64, 158, 255, 0.9);"
        "  color: white;"
        "  border: none;"
        "  border-radius: 16px;"
        "  font-size: 16px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: rgba(102, 177, 255, 1);"
        "}"
        "QPushButton:pressed {"
        "  background-color: rgba(58, 142, 230, 1);"
        "}"
    );

    connect(m_detachButton, &QPushButton::clicked, this, &MainWindow::onDetachButtonClicked);

    // 监听StackedWidget大小变化
    ui->stackedWidget_pages->installEventFilter(this);
    updateDetachButtonPosition();
}

void MainWindow::updateDetachButtonPosition()
{
    if (m_detachButton && ui->stackedWidget_pages) {
        int x = ui->stackedWidget_pages->width() - m_detachButton->width() - 15;
        int y = 15;
        m_detachButton->move(x, y);
        m_detachButton->raise();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateDetachButtonPosition();
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == ui->stackedWidget_pages && event->type() == QEvent::Resize) {
        updateDetachButtonPosition();
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::onDetachButtonClicked()
{
    int currentRow = ui->listWidget_pages->currentRow();
    if (currentRow >= 0 && !m_detachedWindows.contains(currentRow)) {
        detachPage(currentRow);
    }
}

void MainWindow::onPageSelectionChanged(int currentRow)
{
    // 如果页面已弹出，激活弹出窗口
    if (m_detachedWindows.contains(currentRow)) {
        m_detachedWindows[currentRow]->raise();
        m_detachedWindows[currentRow]->activateWindow();
        return;
    }

    // 通过widget查找实际索引（避免弹出后索引错乱）
    QWidget *page = m_pageWidgets.value(currentRow);
    if (page) {
        int stackIndex = ui->stackedWidget_pages->indexOf(page);
        if (stackIndex >= 0) {
            ui->stackedWidget_pages->setCurrentIndex(stackIndex);
        }
    }

    // 切换后重新显示悬浮按钮
    if (m_detachButton) {
        m_detachButton->raise();
    }
}

void MainWindow::onPageDoubleClicked(QListWidgetItem *item)
{
    int pageIndex = ui->listWidget_pages->row(item);

    // 已弹出则激活窗口
    if (m_detachedWindows.contains(pageIndex)) {
        m_detachedWindows[pageIndex]->raise();
        m_detachedWindows[pageIndex]->activateWindow();
        return;
    }

    detachPage(pageIndex);
}

void MainWindow::detachPage(int pageIndex)
{
    if (!m_pageWidgets.contains(pageIndex)) return;

    QWidget *page = m_pageWidgets[pageIndex];
    QString title = m_pageNames.value(pageIndex, "页面");

    // 从StackedWidget移除
    ui->stackedWidget_pages->removeWidget(page);

    // 创建独立窗口
    DetachedWindow *win = new DetachedWindow(pageIndex, page, title, nullptr);
    connect(win, &DetachedWindow::windowClosed, this, &MainWindow::onDetachedWindowClosed);

    m_detachedWindows[pageIndex] = win;
    win->show();

    // 更新侧边栏显示（标记为已弹出）
    QListWidgetItem *listItem = ui->listWidget_pages->item(pageIndex);
    if (listItem) {
        listItem->setText(title + " [已弹出]");
    }

    qDebug() << "[MainWindow] Page detached:" << title;
}

void MainWindow::reattachPage(int pageIndex)
{
    if (!m_detachedWindows.contains(pageIndex)) return;

    DetachedWindow *win = m_detachedWindows[pageIndex];
    QWidget *page = win->takeWidget();

    // 重新添加到StackedWidget（使用addWidget，导航时通过indexOf查找）
    ui->stackedWidget_pages->addWidget(page);

    // 恢复侧边栏文字
    QString title = m_pageNames.value(pageIndex, "页面");
    QListWidgetItem *listItem = ui->listWidget_pages->item(pageIndex);
    if (listItem) {
        listItem->setText(title);
    }

    m_detachedWindows.remove(pageIndex);
    win->deleteLater();

    qDebug() << "[MainWindow] Page reattached:" << title;
}

void MainWindow::onDetachedWindowClosed(int pageIndex)
{
    reattachPage(pageIndex);
}
