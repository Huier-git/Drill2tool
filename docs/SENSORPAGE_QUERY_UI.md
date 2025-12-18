# SensorPage 查询功能使用说明

## 功能概述

SensorPage 右侧的"数据查询"面板提供了便捷的历史数据查询功能，可以快速浏览已采集的数据并查看详细信息。

## 界面组件

### 1. 轮次选择区域
- **选择轮次下拉框 (combo_rounds)**: 显示所有已完成的采集轮次
  - 格式: "Round {轮次ID} - {状态}"
  - 例如: "Round 1 - completed"
- **刷新按钮 (btn_refresh_rounds)**: 重新加载轮次列表

### 2. 时间窗口列表
- **窗口列表 (list_windows)**: 显示选中轮次的所有1秒时间窗口
  - 格式: "yyyy-MM-dd hh:mm:ss"
  - 每行代表一个时间窗口（1秒数据）
  - 双击任意窗口查看该窗口的详细数据

### 3. 查询结果显示
- **结果文本框 (text_query_result)**: 显示选中窗口的所有传感器数据
  - 只读模式
  - 支持滚动浏览
  - 显示三类数据：振动、MDB传感器、电机参数

## 使用步骤

### 基本查询流程

```
1. 点击"刷新"按钮 → 加载所有轮次
2. 选择一个轮次 → 自动加载该轮次的时间窗口列表
3. 双击一个时间窗口 → 查看该窗口的详细数据
```

### 操作示例

**场景1: 查看最近一次采集的数据**
```
操作员: "李明"
轮次备注: "岩心#001 - 第一次钻探"

1. 点击【刷新】
2. 在下拉框选择 "Round 1 - completed"
3. 窗口列表显示所有采集的1秒窗口
4. 双击最后一个窗口（最新数据）
5. 查询结果显示该秒内的所有传感器数据
```

**场景2: 对比不同时刻的振动数据**
```
1. 选择轮次 "Round 1"
2. 双击窗口 "2025-01-25 10:30:15"
3. 查看振动统计: X轴 Min/Max/Mean
4. 双击窗口 "2025-01-25 10:32:20"
5. 对比振动数值变化
```

## 查询结果格式

### 振动数据 (VK701)

```
【振动数据】
  X轴: 5000 个采样点
    Min: -0.125, Max: 0.138, Mean: 0.002
  Y轴: 5000 个采样点
    Min: -0.098, Max: 0.112, Mean: -0.001
  Z轴: 5000 个采样点
    Min: -0.142, Max: 0.156, Mean: 0.003
```

**说明**:
- 采样点数量 = 5000Hz × 1秒 = 5000
- Min/Max/Mean: 预计算的统计值，无需解析BLOB
- 单位: 根据传感器标定（通常为 m/s²）

### MDB传感器数据 (Modbus TCP)

```
【MDB传感器数据】
  上拉力: 10 个采样点
    数值: 125.50 128.30 130.20 ...
  下拉力: 10 个采样点
    数值: 98.40 97.50 96.80 ...
  扭矩: 10 个采样点
    数值: 15.6 16.2 15.9 ...
  位置: 10 个采样点
    数值: 1024.5 1025.8 1027.2 ...
```

**说明**:
- 采样点数量 = 10Hz × 1秒 = 10
- 显示前5个数值 + "..." 表示更多数据
- 单位: 根据传感器类型（N, N·m, mm等）

### 电机参数数据 (ZMotion)

```
【电机参数数据】
  位置: 100 个采样点
  速度: 100 个采样点
  扭矩: 100 个采样点
  电流: 100 个采样点
```

**说明**:
- 采样点数量 = 100Hz × 1秒 = 100
- 仅显示数据量，不展开具体数值
- 8个电机 × 4个参数 = 32组数据

## 技术特性

### 时间对齐机制

所有不同频率的数据都对齐到同一个1秒窗口：

```
时间轴: |------ 窗口1 (0-1s) ------|------ 窗口2 (1-2s) ------|
振动:   |5000个点                  |5000个点                  |
MDB:    |10个点                    |10个点                    |
电机:   |100个点                   |100个点                   |
```

**窗口对齐算法**:
```cpp
qint64 windowStart = (timestampUs / 1000000) * 1000000;
// 例如: 1737780015234567 μs → 1737780015000000 μs (向下取整到秒边界)
```

### 数据库查询逻辑

```cpp
1. 根据轮次ID和窗口起始时间查询 window_id
   SELECT window_id FROM time_windows 
   WHERE round_id=? AND window_start_us=?

2. 根据 window_id 查询振动数据
   SELECT * FROM vibration_blocks WHERE window_id=?
   解析 BLOB 得到原始波形

3. 根据 window_id 查询标量数据
   SELECT * FROM scalar_samples WHERE window_id=?
   按 sensor_type 分组
```

### 性能优化

- **窗口缓存**: 最近100个窗口ID缓存在内存
- **统计预计算**: 振动数据的 min/max/mean/rms 写入时计算
- **按需解析**: 只有被查询的窗口才解析BLOB
- **索引加速**: `idx_tw_round_start` 确保快速定位窗口

## 常见问题

### Q1: 为什么窗口列表是空的？

**可能原因**:
- 该轮次还没有采集数据
- 轮次状态不是 "completed"
- 数据库文件路径错误

**解决方法**:
```
1. 确认轮次已完成采集（点击"结束轮次"）
2. 检查 database/drill_data.db 是否存在
3. 查看控制台日志是否有错误信息
```

### Q2: 双击窗口无反应？

**可能原因**:
- 窗口数据未写入成功
- DataQuerier 未初始化

**解决方法**:
```
1. 检查控制台是否有 "DataQuerier initialized successfully"
2. 查看数据库中 vibration_blocks/scalar_samples 表是否有数据
3. 重启软件重新尝试
```

### Q3: 振动数据显示的采样点数不足5000？

**可能原因**:
- 数据采集中断
- 网络丢包
- 缓冲区溢出

**解决方法**:
```
1. 检查 VK701 连接状态
2. 降低采样频率测试
3. 查看 label_statistics 显示的丢包率
```

## 代码接口

### 主要方法

```cpp
// 加载轮次列表
void SensorPage::loadRoundsList();

// 加载时间窗口列表
void SensorPage::loadWindowsList(int roundId);

// 显示窗口详细数据
void SensorPage::displayWindowData(int roundId, qint64 windowStartUs);
```

### 信号连接

```cpp
// 刷新按钮
connect(ui->btn_refresh_rounds, &QPushButton::clicked, 
        this, &SensorPage::onRefreshRounds);

// 轮次选择
connect(ui->combo_rounds, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &SensorPage::onRoundSelected);

// 窗口双击
connect(ui->list_windows, &QListWidget::itemDoubleClicked,
        this, &SensorPage::onWindowDoubleClicked);
```

## 扩展功能建议

### 未来可能的改进

1. **数据导出**
   - 添加"导出CSV"按钮
   - 支持导出选中窗口或整个轮次

2. **数据可视化**
   - 嵌入 QCustomPlot 绘制波形
   - 实时预览振动频谱

3. **高级查询**
   - 按时间范围查询
   - 按数值阈值过滤
   - 跨轮次对比分析

4. **事件标记**
   - 手动标记异常事件
   - 自动检测突变点
   - 关联标记与窗口

## 相关文档

- [数据库架构说明](./IMPLEMENTATION_COMPLETE.md)
- [查询API使用示例](./QUERY_USAGE_EXAMPLE.md)
- [数据采集流程](./REFACTORING_DESIGN.md)

---

**最后更新**: 2025-01-25  
**版本**: v1.0  
**状态**: ✅ 功能已实现并集成到UI
