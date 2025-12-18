#include "Logger.h"
#include <QTextStream>
#include <QMutexLocker>
#include <iostream>

// 静态成员初始化
QMutex Logger::s_mutex;

void Logger::install()
{
    qInstallMessageHandler(messageHandler);
}

QString Logger::formatMessage(LogLevel level, const QString& module, const QString& message)
{
    // 时间戳格式：HH:mm:ss.zzz
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");

    // 日志级别（固定宽度5字符）
    QString levelStr = levelToString(level).leftJustified(5);

    // 模块名（固定宽度20字符，超长截断）
    QString moduleStr = module;
    if (moduleStr.length() > 20) {
        moduleStr = moduleStr.left(17) + "...";
    }
    moduleStr = moduleStr.leftJustified(20);

    // 格式：[时间][级别][模块] 消息
    return QString("[%1][%2][%3] %4")
        .arg(timestamp)
        .arg(levelStr)
        .arg(moduleStr)
        .arg(message);
}

QString Logger::levelToString(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug:    return "DEBUG";
    case LogLevel::Info:     return "INFO";
    case LogLevel::Warning:  return "WARN";
    case LogLevel::Error:    return "ERROR";
    case LogLevel::Critical: return "CRIT";
    default:                 return "UNKNOWN";
    }
}

void Logger::log(LogLevel level, const QString& module, const QString& message)
{
    QMutexLocker locker(&s_mutex);

    QString formatted = formatMessage(level, module, message);

    // 根据日志级别选择不同的输出流
    switch (level) {
    case LogLevel::Debug:
    case LogLevel::Info:
        qDebug().noquote() << formatted;
        break;
    case LogLevel::Warning:
        qWarning().noquote() << formatted;
        break;
    case LogLevel::Error:
    case LogLevel::Critical:
        qCritical().noquote() << formatted;
        break;
    }
}

void Logger::messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QMutexLocker locker(&s_mutex);

    // 如果消息已经是我们格式化过的（包含时间戳），直接输出
    if (msg.startsWith("[") && msg.contains("][")) {
        QTextStream(stdout) << msg << Qt::endl;
        return;
    }

    // 否则，尝试从context提取模块信息并格式化
    QString module = "Unknown";
    if (context.file) {
        // 从文件名提取模块名（去掉路径和扩展名）
        QString filename = QString(context.file);
        int lastSlash = filename.lastIndexOf('/');
        if (lastSlash == -1) lastSlash = filename.lastIndexOf('\\');
        if (lastSlash != -1) filename = filename.mid(lastSlash + 1);

        int lastDot = filename.lastIndexOf('.');
        if (lastDot != -1) filename = filename.left(lastDot);

        module = filename;
    }

    // 根据Qt消息类型映射到我们的日志级别
    LogLevel level;
    switch (type) {
    case QtDebugMsg:
        level = LogLevel::Debug;
        break;
    case QtInfoMsg:
        level = LogLevel::Info;
        break;
    case QtWarningMsg:
        level = LogLevel::Warning;
        break;
    case QtCriticalMsg:
        level = LogLevel::Error;
        break;
    case QtFatalMsg:
        level = LogLevel::Critical;
        break;
    default:
        level = LogLevel::Debug;
        break;
    }

    QString formatted = formatMessage(level, module, msg);
    QTextStream(stdout) << formatted << Qt::endl;

    // Fatal错误需要终止程序
    if (type == QtFatalMsg) {
        abort();
    }
}
