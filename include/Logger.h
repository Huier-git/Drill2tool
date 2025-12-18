#ifndef LOGGER_H
#define LOGGER_H

#include <QString>
#include <QDebug>
#include <QDateTime>
#include <QMutex>

/**
 * @brief 统一日志系统
 *
 * 提供格式化的日志输出，格式：[时间][级别][模块] 消息内容
 *
 * 使用方式：
 *   LOG_DEBUG("MyClass", "Debug message");
 *   LOG_INFO("MyClass", "Info message");
 *   LOG_WARNING("MyClass", "Warning message");
 *   LOG_ERROR("MyClass", "Error message");
 *   LOG_CRITICAL("MyClass", "Critical message");
 *
 * 或使用流式语法：
 *   LOG_DEBUG_STREAM("MyClass") << "Value:" << value;
 */

// 日志级别枚举
enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
    Critical
};

// 日志工具类
class Logger
{
public:
    /**
     * @brief 安装全局日志处理器（在main函数中调用一次）
     */
    static void install();

    /**
     * @brief 格式化日志消息
     * @param level 日志级别
     * @param module 模块名称
     * @param message 日志消息
     * @return 格式化后的日志字符串
     */
    static QString formatMessage(LogLevel level, const QString& module, const QString& message);

    /**
     * @brief 日志级别转字符串
     */
    static QString levelToString(LogLevel level);

    /**
     * @brief 输出日志
     */
    static void log(LogLevel level, const QString& module, const QString& message);

private:
    static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);
    static QMutex s_mutex;
};

// ==================================================
// 便利宏定义
// ==================================================

// 字符串格式日志
#define LOG_DEBUG(module, message) \
    Logger::log(LogLevel::Debug, module, message)

#define LOG_INFO(module, message) \
    Logger::log(LogLevel::Info, module, message)

#define LOG_WARNING(module, message) \
    Logger::log(LogLevel::Warning, module, message)

#define LOG_ERROR(module, message) \
    Logger::log(LogLevel::Error, module, message)

#define LOG_CRITICAL(module, message) \
    Logger::log(LogLevel::Critical, module, message)

// 流式语法日志辅助类
class LogStream
{
public:
    LogStream(LogLevel level, const QString& module)
        : m_level(level), m_module(module), m_stream(&m_buffer) {}

    ~LogStream() {
        Logger::log(m_level, m_module, m_buffer.trimmed());
    }

    template<typename T>
    LogStream& operator<<(const T& value) {
        m_stream << value;
        return *this;
    }

private:
    LogLevel m_level;
    QString m_module;
    QString m_buffer;
    QDebug m_stream;
};

// 流式语法宏
#define LOG_DEBUG_STREAM(module) \
    LogStream(LogLevel::Debug, module)

#define LOG_INFO_STREAM(module) \
    LogStream(LogLevel::Info, module)

#define LOG_WARNING_STREAM(module) \
    LogStream(LogLevel::Warning, module)

#define LOG_ERROR_STREAM(module) \
    LogStream(LogLevel::Error, module)

#define LOG_CRITICAL_STREAM(module) \
    LogStream(LogLevel::Critical, module)

#endif // LOGGER_H
