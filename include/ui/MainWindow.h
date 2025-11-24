#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QListWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class AcquisitionManager;

/**
 * @brief 主窗口类 - 工业级多页面管理框架
 *
 * 功能：
 * 1. 侧边栏页面切换
 * 2. 多功能子界面管理
 * 3. 状态栏信息显示
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onPageSelectionChanged(int currentRow);

private:
    void setupConnections();
    void initializeAcquisitionSystem();

private:
    Ui::MainWindow *ui;
    AcquisitionManager *m_acquisitionManager;
};

#endif // MAINWINDOW_H
