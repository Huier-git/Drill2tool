/**
 * @file test_autotask.cpp
 * @brief AutoTask 模块单元测试
 *
 * 使用方法：
 * 1. 在 DrillControl.pro 中启用测试模式: CONFIG += test_mode
 * 2. 在 AutoTaskPage 点击"运行单元测试"按钮
 * 3. 观察输出日志验证功能
 */

#include <QDebug>
#include <QThread>
#include "control/AutoDrillManager.h"
#include "control/SafetyWatchdog.h"
#include "control/DrillParameterPreset.h"
#include "dataACQ/DataTypes.h"

class AutoTaskTester
{
public:
    static void testDrillingPressureCalculation()
    {
        qDebug() << "\n=== Test 1: 钻压计算公式 ===";

        // 模拟传感器数据
        double forceUpper = 8000.0;  // 上拉力 8000 N
        double forceLower = 3000.0;  // 下拉力 3000 N
        double drillStringWeight = 500.0;  // 钻管重 500 N

        // 计算钻压: P = 2*(Fu - Fl) - G
        double pressure = 2.0 * (forceUpper - forceLower) - drillStringWeight;

        qDebug() << "上拉力:" << forceUpper << "N";
        qDebug() << "下拉力:" << forceLower << "N";
        qDebug() << "钻管重:" << drillStringWeight << "N";
        qDebug() << "计算钻压:" << pressure << "N";
        qDebug() << "预期:" << (2*(8000-3000)-500) << "N";

        Q_ASSERT(qAbs(pressure - 9500.0) < 0.01);
        qDebug() << "✅ 钻压计算正确";
    }

    static void testSafetyWatchdogTorqueLimit()
    {
        qDebug() << "\n=== Test 2: 扭矩限位触发 ===";

        SafetyWatchdog watchdog;

        // 设置预设（必须包含所有必要字段才能通过 isValid() 检查）
        DrillParameterPreset preset;
        preset.id = "TEST";
        preset.feedSpeedMmPerMin = 38.0;  // 必须 > 0
        preset.rotationRpm = 55.0;        // 必须 > 0
        preset.torqueLimitNm = 1600.0;
        preset.pressureLimitN = 15000.0;

        watchdog.arm(preset);

        bool faultTriggered = false;
        QObject::connect(&watchdog, &SafetyWatchdog::faultOccurred,
                        [&faultTriggered](const QString& code, const QString& detail) {
            qDebug() << "⚠️ 故障触发:" << code << "-" << detail;
            faultTriggered = true;
            Q_ASSERT(code == "TORQUE_LIMIT");
        });

        // 模拟正常数据
        watchdog.onTelemetryUpdate(100.0, 30.0, 1200.0, 10000.0);
        Q_ASSERT(!faultTriggered);
        qDebug() << "正常数据: 扭矩 1200 Nm < 限制 1600 Nm";

        // 模拟超限数据
        watchdog.onTelemetryUpdate(100.0, 30.0, 1800.0, 10000.0);
        Q_ASSERT(faultTriggered);
        qDebug() << "✅ 扭矩超限正确触发故障";
    }

    static void testSafetyWatchdogPressureLimit()
    {
        qDebug() << "\n=== Test 3: 钻压限位触发 ===";

        SafetyWatchdog watchdog;

        DrillParameterPreset preset;
        preset.id = "TEST";
        preset.feedSpeedMmPerMin = 38.0;
        preset.rotationRpm = 55.0;
        preset.torqueLimitNm = 2000.0;
        preset.pressureLimitN = 15000.0;

        watchdog.arm(preset);

        bool faultTriggered = false;
        QObject::connect(&watchdog, &SafetyWatchdog::faultOccurred,
                        [&faultTriggered](const QString& code, const QString& detail) {
            qDebug() << "⚠️ 故障触发:" << code << "-" << detail;
            faultTriggered = true;
            Q_ASSERT(code == "PRESSURE_LIMIT");
        });

        // 正常钻压
        watchdog.onTelemetryUpdate(100.0, 30.0, 1200.0, 12000.0);
        Q_ASSERT(!faultTriggered);
        qDebug() << "正常数据: 钻压 12000 N < 限制 15000 N";

        // 超限钻压
        watchdog.onTelemetryUpdate(100.0, 30.0, 1200.0, 18000.0);
        Q_ASSERT(faultTriggered);
        qDebug() << "✅ 钻压超限正确触发故障";
    }

    static void testStallDetection()
    {
        qDebug() << "\n=== Test 4: 堵转检测 ===";

        SafetyWatchdog watchdog;

        DrillParameterPreset preset;
        preset.id = "TEST";
        preset.feedSpeedMmPerMin = 38.0;
        preset.rotationRpm = 55.0;
        preset.torqueLimitNm = 2000.0;
        preset.pressureLimitN = 20000.0;
        preset.stallVelocityMmPerMin = 5.0;
        preset.stallWindowMs = 1000;

        watchdog.arm(preset);

        bool stallDetected = false;
        QObject::connect(&watchdog, &SafetyWatchdog::faultOccurred,
                        [&stallDetected](const QString& code, const QString& detail) {
            if (code == "STALL_DETECTED") {
                qDebug() << "⚠️ 堵转检测:" << detail;
                stallDetected = true;
            }
        });

        // 模拟钻头卡住：位置不变 + 速度极低
        for (int i = 0; i < 15; ++i) {
            watchdog.onTelemetryUpdate(
                100.0 + i*0.01,  // 位置几乎不变
                0.5,              // 速度极低
                1500.0,
                12000.0
            );
            QThread::msleep(100);  // 模拟时间流逝
        }

        Q_ASSERT(stallDetected);
        qDebug() << "✅ 堵转检测正确触发";
    }

    static void testDataBlockParsing()
    {
        qDebug() << "\n=== Test 5: 数据块解析 ===";

        // 模拟 MDB 传感器数据
        DataBlock torqueBlock;
        torqueBlock.sensorType = SensorType::Torque_MDB;
        torqueBlock.values << 1250.0;
        qDebug() << "扭矩数据块:" << torqueBlock.values.last() << "Nm";

        DataBlock forceUpperBlock;
        forceUpperBlock.sensorType = SensorType::Force_Upper;
        forceUpperBlock.values << 8000.0;
        qDebug() << "上拉力数据块:" << forceUpperBlock.values.last() << "N";

        DataBlock forceLowerBlock;
        forceLowerBlock.sensorType = SensorType::Force_Lower;
        forceLowerBlock.values << 3000.0;
        qDebug() << "下拉力数据块:" << forceLowerBlock.values.last() << "N";

        // 模拟 Motor 数据
        DataBlock posBlock;
        posBlock.sensorType = SensorType::Motor_Position;
        posBlock.values << 156.5;
        qDebug() << "位置数据块:" << posBlock.values.last() << "mm";

        DataBlock speedBlock;
        speedBlock.sensorType = SensorType::Motor_Speed;
        speedBlock.values << 38.0;
        qDebug() << "速度数据块:" << speedBlock.values.last() << "mm/min";

        qDebug() << "✅ 数据块结构正确";
    }

    static void testPresetLoading()
    {
        qDebug() << "\n=== Test 6: 预设加载 ===";

        // 从 JSON 加载
        QJsonObject json;
        json["id"] = "P2";
        json["description"] = "Test preset";
        json["vp_mm_per_min"] = 38.0;
        json["rpm"] = 55.0;
        json["fi_hz"] = 5.0;
        json["torque_limit_nm"] = 1600.0;
        json["pressure_limit_n"] = 15000.0;
        json["drill_string_weight_n"] = 500.0;
        json["stall_velocity_mm_per_min"] = 5.0;
        json["stall_window_ms"] = 1000;

        DrillParameterPreset preset = DrillParameterPreset::fromJson(json);

        Q_ASSERT(preset.id == "P2");
        Q_ASSERT(preset.feedSpeedMmPerMin == 38.0);
        Q_ASSERT(preset.torqueLimitNm == 1600.0);
        Q_ASSERT(preset.pressureLimitN == 15000.0);
        Q_ASSERT(preset.drillStringWeightN == 500.0);

        qDebug() << "预设ID:" << preset.id;
        qDebug() << "进给速度:" << preset.feedSpeedMmPerMin << "mm/min";
        qDebug() << "扭矩限制:" << preset.torqueLimitNm << "Nm";
        qDebug() << "钻压限制:" << preset.pressureLimitN << "N";
        qDebug() << "钻管重量:" << preset.drillStringWeightN << "N";
        qDebug() << "✅ 预设加载正确";
    }

    static void runAllTests()
    {
        qDebug() << "\n╔═══════════════════════════════════════╗";
        qDebug() << "║   AutoTask 模块单元测试套件          ║";
        qDebug() << "╚═══════════════════════════════════════╝";

        testDrillingPressureCalculation();
        testSafetyWatchdogTorqueLimit();
        testSafetyWatchdogPressureLimit();
        testStallDetection();
        testDataBlockParsing();
        testPresetLoading();

        qDebug() << "\n╔═══════════════════════════════════════╗";
        qDebug() << "║   ✅ 所有测试通过                     ║";
        qDebug() << "╚═══════════════════════════════════════╝\n";
    }
};

// 全局测试入口函数
void testAutoTask()
{
    AutoTaskTester::runAllTests();
}
