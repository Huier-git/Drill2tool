#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QListWidget>
#include <QMap>
#include <QPushButton>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class AcquisitionManager;
class SensorPage;
class VibrationPage;
class MdbPage;
class MotorPage;
class ControlPage;
class DatabasePage;
class DrillControlPage;
class PlanVisualizerPage;
class AutoTaskPage;
class DetachedWindow;

/**
 * @brief 主窗口类 - 工业级多页面管理框架
 *
 * 功能：
 * 1. 侧边栏页面切换
 * 2. 多功能子界面管理
 * 3. 双击弹出独立窗口，关闭后回收
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onPageSelectionChanged(int currentRow);
    void onPageDoubleClicked(QListWidgetItem *item);
    void onDetachedWindowClosed(int pageIndex);
    void onDetachButtonClicked();

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void setupConnections();
    void setupAcquisitionManager();
    void setupPages();
    void setupDetachButton();
    void detachPage(int pageIndex);
    void reattachPage(int pageIndex);
    void updateDetachButtonPosition();

private:
    Ui::MainWindow *ui;
    AcquisitionManager *m_acquisitionManager;
    SensorPage *m_sensorPage;
    VibrationPage *m_vibrationPage;
    MdbPage *m_mdbPage;
    MotorPage *m_motorPage;
    ControlPage *m_controlPage;
    DatabasePage *m_databasePage;
    DrillControlPage *m_drillControlPage;
    PlanVisualizerPage *m_planVisualizerPage;
    AutoTaskPage *m_autoTaskPage;

    // 弹出窗口管理
    QMap<int, DetachedWindow*> m_detachedWindows;  // pageIndex -> window
    QMap<int, QWidget*> m_pageWidgets;             // pageIndex -> original widget
    QStringList m_pageNames;
    QPushButton *m_detachButton;                   // 悬浮弹出按钮
};

/**
 * @brief 独立弹出窗口容器
 */
class DetachedWindow : public QWidget
{
    Q_OBJECT

public:
    DetachedWindow(int pageIndex, QWidget *page, const QString &title, QWidget *parent = nullptr);
    ~DetachedWindow();

    QWidget* takeWidget();
    int pageIndex() const { return m_pageIndex; }

signals:
    void windowClosed(int pageIndex);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    int m_pageIndex;
    QWidget *m_page;
};

#endif // MAINWINDOW_H
