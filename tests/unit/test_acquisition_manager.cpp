/**
 * @file test_acquisition_manager.cpp
 * @brief AcquisitionManager 回归测试
 *
 * 目标：验证数据库无法打开时，采集不会启动且会产生明确的错误提示。
 */

#include <QDebug>
#include <QtGlobal>
#include <QObject>
#include "control/AcquisitionManager.h"

class AcquisitionManagerTester
{
public:
    static void testDatabaseOpenFailure()
    {
        qDebug() << "\n=== Test: 数据库打开失败时的采集保护 ===";

        AcquisitionManager manager;
        bool errorSignaled = false;

        QObject::connect(&manager, &AcquisitionManager::errorOccurred,
                         [&errorSignaled](const QString &worker, const QString &error) {
            qDebug() << "捕获错误信号:" << worker << "-" << error;
            errorSignaled = true;
        });

        // 使用无效路径模拟数据库无法打开
        manager.initialize("/this/path/should/not/exist/drill_data.db");
        manager.startAll();

        Q_ASSERT(!manager.isRunning());
        Q_ASSERT(errorSignaled);

        manager.shutdown();
        qDebug() << "✅ 数据库打开失败时采集不会启动且用户得到错误提示";
    }

    static void runAllTests()
    {
        testDatabaseOpenFailure();
    }
};

// 全局测试入口函数（与现有 AutoTask 测试风格保持一致）
void testAcquisitionManager()
{
    AcquisitionManagerTester::runAllTests();
}
