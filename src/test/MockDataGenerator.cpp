/**
 * @file MockDataGenerator.cpp
 * @brief 模拟传感器数据生成器实现
 */

#include "test/MockDataGenerator.h"
#include <QDebug>
#include <QtMath>

MockDataGenerator::MockDataGenerator(QObject* parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
    , m_scenario(SimulationScenario::NormalDrilling)
    , m_tickCount(0)
    , m_currentDepth(0.0)
    , m_currentVelocity(0.0)
    , m_currentTorque(0.0)
    , m_currentForceUpper(0.0)
    , m_currentForceLower(0.0)
{
    connect(m_timer, &QTimer::timeout, this, &MockDataGenerator::generateNextData);
}

void MockDataGenerator::setScenario(SimulationScenario scenario)
{
    m_scenario = scenario;

    QString description;
    switch (scenario) {
    case SimulationScenario::NormalDrilling:
        description = "正常钻进场景";
        break;
    case SimulationScenario::TorqueOverload:
        description = "扭矩超限场景（遇到硬层）";
        break;
    case SimulationScenario::PressureOverload:
        description = "钻压超限场景（推力过大）";
        break;
    case SimulationScenario::Stall:
        description = "堵转场景（钻头卡住）";
        break;
    case SimulationScenario::ProgressiveFailure:
        description = "逐步恶化场景（正常→异常→故障）";
        break;
    }

    emit scenarioChanged(description);
    qDebug() << "[MockDataGenerator] 场景切换:" << description;
}

void MockDataGenerator::setUpdateInterval(int msec)
{
    m_timer->setInterval(msec);
}

void MockDataGenerator::startSimulation()
{
    m_tickCount = 0;
    m_currentDepth = 50.0;        // 起始深度 50mm
    m_currentVelocity = 38.0;     // 初始速度 38 mm/min
    m_currentTorque = 800.0;      // 初始扭矩 800 Nm
    m_currentForceUpper = 6000.0; // 上拉力 6000 N
    m_currentForceLower = 2500.0; // 下拉力 2500 N

    m_timer->start();
    qDebug() << "[MockDataGenerator] 模拟开始，间隔:" << m_timer->interval() << "ms";
}

void MockDataGenerator::stopSimulation()
{
    m_timer->stop();
    qDebug() << "[MockDataGenerator] 模拟停止";
}

void MockDataGenerator::generateNextData()
{
    m_tickCount++;

    switch (m_scenario) {
    case SimulationScenario::NormalDrilling:
        generateNormalDrilling();
        break;
    case SimulationScenario::TorqueOverload:
        generateTorqueOverload();
        break;
    case SimulationScenario::PressureOverload:
        generatePressureOverload();
        break;
    case SimulationScenario::Stall:
        generateStall();
        break;
    case SimulationScenario::ProgressiveFailure:
        generateProgressiveFailure();
        break;
    }

    // 发送数据块
    emitDataBlock(SensorType::Motor_Position, m_currentDepth);
    emitDataBlock(SensorType::Motor_Speed, m_currentVelocity);
    emitDataBlock(SensorType::Torque_MDB, m_currentTorque);
    emitDataBlock(SensorType::Force_Upper, m_currentForceUpper);
    emitDataBlock(SensorType::Force_Lower, m_currentForceLower);
}

void MockDataGenerator::generateNormalDrilling()
{
    // 正常钻进：匀速下降，参数稳定
    double deltaTime = m_timer->interval() / 1000.0;  // 秒
    m_currentDepth += m_currentVelocity / 60.0 * deltaTime;  // mm/min → mm/s

    // 添加小波动模拟真实情况
    double noise = (qrand() % 100 - 50) / 100.0;  // ±0.5

    m_currentVelocity = 38.0 + noise;
    m_currentTorque = 1200.0 + noise * 50;
    m_currentForceUpper = 7000.0 + noise * 100;
    m_currentForceLower = 2800.0 + noise * 80;

    if (m_tickCount % 50 == 0) {
        qDebug() << QString("[正常钻进] 深度:%1mm 速度:%2mm/min 扭矩:%3Nm")
                    .arg(m_currentDepth, 0, 'f', 1)
                    .arg(m_currentVelocity, 0, 'f', 1)
                    .arg(m_currentTorque, 0, 'f', 0);
    }
}

void MockDataGenerator::generateTorqueOverload()
{
    // 扭矩超限：遇到硬层，扭矩逐渐上升
    double deltaTime = m_timer->interval() / 1000.0;
    m_currentDepth += m_currentVelocity / 60.0 * deltaTime;

    // 扭矩逐渐升高
    if (m_tickCount < 30) {
        m_currentTorque = 1200.0 + m_tickCount * 20;  // 逐渐增加
        m_currentVelocity = 38.0;
    } else {
        m_currentTorque = 1800.0 + (qrand() % 100);   // 超限！
        m_currentVelocity = 20.0;  // 速度下降
    }

    m_currentForceUpper = 7500.0;
    m_currentForceLower = 3000.0;

    if (m_tickCount % 10 == 0) {
        qDebug() << QString("[扭矩超限] Tick:%1 扭矩:%2Nm (限制:1600Nm)")
                    .arg(m_tickCount)
                    .arg(m_currentTorque, 0, 'f', 0);
    }
}

void MockDataGenerator::generatePressureOverload()
{
    // 钻压超限：推力过大
    double deltaTime = m_timer->interval() / 1000.0;
    m_currentDepth += m_currentVelocity / 60.0 * deltaTime;

    m_currentVelocity = 38.0;
    m_currentTorque = 1300.0;

    // 钻压逐渐增加
    // 钻压 = 2*(Fu - Fl) - G
    // 假设 G = 500N，要超过 15000N
    if (m_tickCount < 20) {
        m_currentForceUpper = 6000.0 + m_tickCount * 200;
        m_currentForceLower = 2500.0;
    } else {
        m_currentForceUpper = 10000.0;  // 2*(10000-2500)-500 = 14500 N (接近限制)
        m_currentForceLower = 2000.0;   // 2*(10000-2000)-500 = 15500 N (超限！)
    }

    double pressure = 2.0 * (m_currentForceUpper - m_currentForceLower) - 500.0;

    if (m_tickCount % 10 == 0) {
        qDebug() << QString("[钻压超限] Tick:%1 钻压:%2N (限制:15000N)")
                    .arg(m_tickCount)
                    .arg(pressure, 0, 'f', 0);
    }
}

void MockDataGenerator::generateStall()
{
    // 堵转：钻头卡住，位置不变，速度极低
    // 位置几乎不变（微小抖动）
    m_currentDepth = 100.0 + (qrand() % 10) / 100.0;  // 100.00 ~ 100.09 mm

    m_currentVelocity = 0.5 + (qrand() % 10) / 100.0;  // 0.5 ~ 0.6 mm/min (极低)
    m_currentTorque = 1400.0;
    m_currentForceUpper = 7000.0;
    m_currentForceLower = 2800.0;

    if (m_tickCount % 10 == 0) {
        qDebug() << QString("[堵转] Tick:%1 深度:%2mm 速度:%3mm/min")
                    .arg(m_tickCount)
                    .arg(m_currentDepth, 0, 'f', 2)
                    .arg(m_currentVelocity, 0, 'f', 2);
    }
}

void MockDataGenerator::generateProgressiveFailure()
{
    // 逐步恶化：正常 → 异常 → 故障
    double deltaTime = m_timer->interval() / 1000.0;

    if (m_tickCount < 50) {
        // 阶段1: 正常钻进
        m_currentDepth += 38.0 / 60.0 * deltaTime;
        m_currentVelocity = 38.0;
        m_currentTorque = 1200.0;
        m_currentForceUpper = 7000.0;
        m_currentForceLower = 2800.0;
    } else if (m_tickCount < 100) {
        // 阶段2: 开始异常
        m_currentDepth += 25.0 / 60.0 * deltaTime;
        m_currentVelocity = 25.0;
        m_currentTorque = 1400.0 + (m_tickCount - 50) * 8;  // 逐渐增加
        m_currentForceUpper = 7500.0 + (m_tickCount - 50) * 30;
        m_currentForceLower = 2800.0;
    } else {
        // 阶段3: 故障（扭矩超限 + 堵转）
        m_currentDepth += 2.0 / 60.0 * deltaTime;  // 几乎不动
        m_currentVelocity = 2.0;
        m_currentTorque = 1900.0;  // 超限！
        m_currentForceUpper = 9000.0;
        m_currentForceLower = 2500.0;
    }

    if (m_tickCount % 20 == 0) {
        QString phase = m_tickCount < 50 ? "正常" : (m_tickCount < 100 ? "异常" : "故障");
        qDebug() << QString("[逐步恶化] Tick:%1 阶段:%2 扭矩:%3Nm 速度:%4mm/min")
                    .arg(m_tickCount)
                    .arg(phase)
                    .arg(m_currentTorque, 0, 'f', 0)
                    .arg(m_currentVelocity, 0, 'f', 1);
    }
}

void MockDataGenerator::emitDataBlock(SensorType type, double value)
{
    DataBlock block;
    block.sensorType = type;
    block.roundId = 1;
    block.channelId = 0;
    block.startTimestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
    block.sampleRate = 1000.0 / m_timer->interval();  // Hz
    block.numSamples = 1;
    block.values << value;

    emit dataBlockReady(block);
}
