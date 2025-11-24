#include "ui/MainWindow.h"

#include <QApplication>
#include <QDebug>
#include <QDateTime>
#include <QTextCodec>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    // 设置应用程序信息
    QApplication::setApplicationName("DrillControl");
    QApplication::setApplicationVersion("2.0.0");
    QApplication::setOrganizationName("KT");
    
    // 输出启动信息
    qDebug() << "==================================";
    qDebug() << "钻机采集控制系统 v2.0";
    qDebug() << "启动时间:" << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    qDebug() << "==================================";
    
    // 创建并显示主窗口
    MainWindow mainWindow;
    mainWindow.show();
    
    qDebug() << "主窗口已显示";
    
    return app.exec();
}
