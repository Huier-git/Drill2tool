#ifndef DATATYPES_H
#define DATATYPES_H

#include <QString>
#include <QVector>
#include <QMetaType>

/**
 * @brief 传感器类型枚举
 */
enum class SensorType {
    // MDB传感器（10Hz）
    Force_Upper = 100,      // 上拉力传感器
    Force_Lower = 101,      // 下拉力传感器
    Torque_MDB = 102,       // 扭矩传感器
    Position_MDB = 103,     // 位置传感器
    
    // 振动传感器（5000Hz）
    Vibration_X = 200,      // X轴振动
    Vibration_Y = 201,      // Y轴振动
    Vibration_Z = 202,      // Z轴振动
    
    // 电机参数（可配置频率）
    Motor_Position = 300,   // 电机位置
    Motor_Speed = 301,      // 电机速度
    Motor_Torque = 302,     // 电机扭矩
    Motor_Current = 303,    // 电机电流
    
    Unknown = 999           // 未知类型
};

/**
 * @brief Worker工作状态
 */
enum class WorkerState {
    Stopped,        // 已停止
    Starting,       // 启动中
    Running,        // 运行中
    Pausing,        // 暂停中
    Paused,         // 已暂停
    Stopping,       // 停止中
    Error           // 错误状态
};

/**
 * @brief 统一数据块结构
 * 
 * 所有采集Worker发送的数据都用这个结构封装
 */
struct DataBlock {
    int roundId;                    // 轮次ID
    SensorType sensorType;          // 传感器类型
    int channelId;                  // 通道ID（电机号、振动通道等）
    qint64 startTimestampUs;        // 起始时间戳（微秒）
    double sampleRate;              // 采样频率（Hz）
    int numSamples;                 // 样本数量
    
    // 数据内容（两种方式二选一）
    QVector<double> values;         // 标量数据（用于10Hz低频数据）
    QByteArray blobData;            // BLOB数据（用于5000Hz高频振动）
    
    // 可选的额外信息
    QString comment;                // 备注信息
    
    DataBlock() 
        : roundId(0)
        , sensorType(SensorType::Unknown)
        , channelId(0)
        , startTimestampUs(0)
        , sampleRate(0.0)
        , numSamples(0)
    {}
};

// 注册到Qt元类型系统，使其可以在信号槽中传递
Q_DECLARE_METATYPE(DataBlock)
Q_DECLARE_METATYPE(SensorType)
Q_DECLARE_METATYPE(WorkerState)

/**
 * @brief 辅助函数：传感器类型转字符串
 */
inline QString sensorTypeToString(SensorType type) {
    switch(type) {
        case SensorType::Force_Upper: return "Force_Upper";
        case SensorType::Force_Lower: return "Force_Lower";
        case SensorType::Torque_MDB: return "Torque_MDB";
        case SensorType::Position_MDB: return "Position_MDB";
        case SensorType::Vibration_X: return "Vibration_X";
        case SensorType::Vibration_Y: return "Vibration_Y";
        case SensorType::Vibration_Z: return "Vibration_Z";
        case SensorType::Motor_Position: return "Motor_Position";
        case SensorType::Motor_Speed: return "Motor_Speed";
        case SensorType::Motor_Torque: return "Motor_Torque";
        case SensorType::Motor_Current: return "Motor_Current";
        default: return "Unknown";
    }
}

/**
 * @brief 辅助函数：Worker状态转字符串
 */
inline QString workerStateToString(WorkerState state) {
    switch(state) {
        case WorkerState::Stopped: return "Stopped";
        case WorkerState::Starting: return "Starting";
        case WorkerState::Running: return "Running";
        case WorkerState::Pausing: return "Pausing";
        case WorkerState::Paused: return "Paused";
        case WorkerState::Stopping: return "Stopping";
        case WorkerState::Error: return "Error";
        default: return "Unknown";
    }
}

#endif // DATATYPES_H
