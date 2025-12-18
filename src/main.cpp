#include "ui/MainWindow.h"
#include "Logger.h"

#include <QApplication>
#include <QDebug>
#include <QDateTime>
#include <QTextCodec>
#include <QFile>

int main(int argc, char *argv[])
{

    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);

    // 安装统一日志系统
    Logger::install();

    // 设置应用程序信息
    QApplication::setApplicationName("DrillControl");
    QApplication::setApplicationVersion("2.0.0");
    QApplication::setOrganizationName("KT");

    // 加载统一按钮样式表
    QFile styleFile(":/styles/styles/buttons.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        QString styleSheet = QString::fromUtf8(styleFile.readAll());
        app.setStyleSheet(styleSheet);
        LOG_DEBUG("Main", "Button stylesheet loaded successfully");
    } else {
        LOG_WARNING("Main", "Failed to load button stylesheet");
    }

    // 输出启动信息
    LOG_INFO("Main", "==================================");
    LOG_INFO("Main", "钻机采集控制系统 v2.0");
    LOG_INFO_STREAM("Main") << "启动时间:" << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    LOG_INFO("Main", "==================================");

    // 创建并显示主窗口
    MainWindow mainWindow;
    mainWindow.show();

    LOG_DEBUG("Main", "主窗口已显示");

    return app.exec();
}
