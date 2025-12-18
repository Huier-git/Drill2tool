# DataQuerier 使用示例

## 核心功能：查询某1秒内的所有数据

DataQuerier 提供按时间窗口查询数据的功能，所有数据自动对齐到1秒窗口。

## 使用示例

### 1. 初始化查询器

```cpp
#include "database/DataQuerier.h"

DataQuerier querier("database/drill_data.db");
if (!querier.initialize()) {
    qWarning() << "Failed to initialize querier";
    return;
}
```

### 2. 获取所有轮次

```cpp
QList<DataQuerier::RoundInfo> rounds = querier.getAllRounds();

for (const auto &round : rounds) {
    qDebug() << "Round ID:" << round.roundId;
    qDebug() << "Start Time:" << round.startTimeUs;
    qDebug() << "Status:" << round.status;
}
```

### 3. 查询某1秒的所有数据（核心功能）

```cpp
int roundId = 1;
qint64 windowStartUs = 1234567000000;  // 某个窗口的起始时间

// 查询该窗口内的所有数据
DataQuerier::WindowData data = querier.getWindowData(roundId, windowStartUs);

// 振动数据（5000Hz × 3通道）
for (int ch = 0; ch < 3; ++ch) {
    if (data.vibrationData.contains(ch)) {
        QVector<float> values = data.vibrationData[ch];
        qDebug() << "Channel" << ch << ":" << values.size() << "samples";
        // 这里有约5000个振动数据点
    }
}

// MDB数据（10Hz × 4传感器）
int sensorTypes[] = {100, 101, 102, 103};  // Force_Upper, Force_Lower, Torque_MDB, Position_MDB
for (int type : sensorTypes) {
    if (data.scalarData.contains(type)) {
        QVector<double> values = data.scalarData[type];
        qDebug() << "Sensor" << type << ":" << values.size() << "samples";
        // 这里有约10个MDB数据点
    }
}

// 电机数据（100Hz × 8电机 × 4参数）
int motorTypes[] = {300, 301, 302, 303};  // Position, Speed, Torque, Current
for (int type : motorTypes) {
    if (data.scalarData.contains(type)) {
        QVector<double> values = data.scalarData[type];
        qDebug() << "Motor param" << type << ":" << values.size() << "samples";
        // 这里有约100个电机数据点
    }
}
```

### 4. 查询时间范围内的数据

```cpp
qint64 startTime = 1234567000000;  // 起始时间
qint64 endTime = 1234570000000;    // 结束时间（3秒后）

QList<DataQuerier::WindowData> dataList = querier.getTimeRangeData(roundId, startTime, endTime);

qDebug() << "Retrieved" << dataList.size() << "windows of data";

// 遍历每个窗口
for (const auto &window : dataList) {
    qDebug() << "Window start:" << window.windowStartUs;
    qDebug() << "Vibration channels:" << window.vibrationData.size();
    qDebug() << "Scalar sensors:" << window.scalarData.size();
}
```

### 5. 快速获取振动统计信息（不解析BLOB）

```cpp
int channelId = 0;  // X轴
qint64 startTime = 1234567000000;
qint64 endTime = 1234570000000;

// 直接读取预计算的统计值，非常快！
QList<DataQuerier::VibrationStats> stats =
    querier.getVibrationStats(roundId, channelId, startTime, endTime);

for (const auto &stat : stats) {
    qDebug() << "Time:" << stat.timestampUs;
    qDebug() << "  Min:" << stat.minValue;
    qDebug() << "  Max:" << stat.maxValue;
    qDebug() << "  Mean:" << stat.meanValue;
    qDebug() << "  RMS:" << stat.rmsValue;
}
```

### 6. 获取窗口时间戳列表

```cpp
QList<qint64> timestamps = querier.getWindowTimestamps(roundId);

qDebug() << "Total windows:" << timestamps.size();

// 遍历所有窗口
for (qint64 ts : timestamps) {
    DataQuerier::WindowData data = querier.getWindowData(roundId, ts);
    // 处理每个窗口的数据
}
```

## 典型应用场景

### 场景1：数据导出为CSV

```cpp
// 导出某轮次的所有数据
QList<qint64> windows = querier.getWindowTimestamps(roundId);

QFile file("export.csv");
file.open(QIODevice::WriteOnly | QIODevice::Text);
QTextStream out(&file);

out << "timestamp_us,vibration_x_mean,vibration_y_mean,vibration_z_mean,"
    << "force_upper,force_lower,torque,position\n";

for (qint64 windowStart : windows) {
    DataQuerier::WindowData data = querier.getWindowData(roundId, windowStart);

    out << windowStart << ",";

    // 振动均值
    for (int ch = 0; ch < 3; ++ch) {
        if (data.vibrationData.contains(ch)) {
            double sum = 0;
            for (float v : data.vibrationData[ch]) sum += v;
            out << (sum / data.vibrationData[ch].size()) << ",";
        } else {
            out << "0,";
        }
    }

    // MDB数据
    for (int type : {100, 101, 102, 103}) {
        if (data.scalarData.contains(type) && !data.scalarData[type].isEmpty()) {
            out << data.scalarData[type].first() << ",";
        } else {
            out << "0,";
        }
    }

    out << "\n";
}
```

### 场景2：实时数据预览

```cpp
// 在UI中显示最新N个窗口的数据
QList<qint64> windows = querier.getWindowTimestamps(roundId);
int N = 10;  // 最近10秒

for (int i = qMax(0, windows.size() - N); i < windows.size(); ++i) {
    qint64 windowStart = windows[i];

    // 使用快速统计查询（不解析BLOB）
    auto stats = querier.getVibrationStats(roundId, 0, windowStart, windowStart + 1000000);

    if (!stats.isEmpty()) {
        // 更新UI：显示RMS值
        ui->chart->addPoint(windowStart, stats.first().rmsValue);
    }
}
```

### 场景3：事件检测

```cpp
// 检测振动异常
QList<qint64> windows = querier.getWindowTimestamps(roundId);

for (qint64 windowStart : windows) {
    auto stats = querier.getVibrationStats(roundId, 0, windowStart, windowStart + 1000000);

    if (!stats.isEmpty() && stats.first().rmsValue > 10.0) {
        qWarning() << "High vibration detected at" << windowStart;
        // 记录事件或触发报警
    }
}
```

## 性能特点

1. **窗口对齐**：所有数据自动按1秒窗口对齐，无需手动插值
2. **预计算统计**：振动数据的min/max/mean/rms已预计算，查询极快
3. **按需解析BLOB**：只有真正需要原始振动数据时才解析BLOB
4. **批量查询**：getTimeRangeData 可一次查询多个窗口

## 注意事项

- 窗口起始时间必须是1秒的整数倍（微秒级）
- 查询不存在的窗口会返回空的 WindowData
- 大量数据查询时建议使用 getTimeRangeData 而不是循环调用 getWindowData
