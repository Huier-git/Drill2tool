/**
 * @file MockDataGenerator.h
 * @brief 模拟传感器数据生成器
 *
 * 用于在没有真实硬件的情况下测试 AutoTask 功能
 *
 * 使用方法：
 * 1. 在测试时创建 MockDataGenerator 实例
 * 2. 连接到 AutoDrillManager::onDataBlockReceived
 * 3. 调用 startSimulation() 开始模拟
 * 4. 可以模拟正常钻进、扭矩超限、钻压超限、堵转等场景
 */

#ifndef MOCKDATAGENERATOR_H
#define MOCKDATAGENERATOR_H

#include <QObject>
#include <QTimer>
#include "dataACQ/DataTypes.h"

class MockDataGenerator : public QObject
{
    Q_OBJECT

public:
    enum class SimulationScenario {
        NormalDrilling,      // 正常钻进
        TorqueOverload,      // 扭矩超限
        PressureOverload,    // 钻压超限
        Stall,               // 堵转
        ProgressiveFailure   // 逐步恶化（先正常，再超限）
    };

    explicit MockDataGenerator(QObject* parent = nullptr);

    // 配置模拟参数
    void setScenario(SimulationScenario scenario);
    void setUpdateInterval(int msec);  // 模拟数据更新频率

    // 控制模拟
    void startSimulation();
    void stopSimulation();

signals:
    // 模拟 Worker 的 dataBlockReady 信号
    void dataBlockReady(const DataBlock& block);
    void scenarioChanged(const QString& description);

private slots:
    void generateNextData();

private:
    QTimer* m_timer;
    SimulationScenario m_scenario;
    int m_tickCount;  // 已生成的数据帧数

    // 当前模拟状态
    double m_currentDepth;      // mm
    double m_currentVelocity;   // mm/min
    double m_currentTorque;     // Nm
    double m_currentForceUpper; // N
    double m_currentForceLower; // N

    void generateNormalDrilling();
    void generateTorqueOverload();
    void generatePressureOverload();
    void generateStall();
    void generateProgressiveFailure();

    void emitDataBlock(SensorType type, double value);
};

#endif // MOCKDATAGENERATOR_H
