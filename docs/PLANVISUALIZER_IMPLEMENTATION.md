# PlanVisualizerPage 实现记录

## 2025-01-25 - 钻杆规划可视化页面实现

### 功能概述
实现了多钻杆规划可视化系统，集成Python规划脚本和Qt原生甘特图可视化。

### 主要功能
1. **规划模式选择**
   - 串行规划 (serial.py)
   - 优化规划 (scheduler.py，使用OR-Tools CP-SAT求解器)

2. **参数配置**
   - 钻杆数量 (1-10根)
   - 缩放系数 (1-5倍)
   - 可编辑的操作时长配置表

3. **可视化输出**
   - ASCII甘特图输出
   - QCustomPlot绘制的交互式甘特图
   - 横向滚动条支持长时间轴展示

4. **时长配置管理**
   - 内嵌默认时长配置
   - 可编辑表格界面
   - 加载/保存/重置配置功能
   - 自动导出durations_override.json供Python使用

5. **规划控制**
   - 运行规划按钮
   - 重新规划按钮
   - 导出JSON功能

### 技术实现

#### 文件结构
```
include/ui/PlanVisualizerPage.h
src/ui/PlanVisualizerPage.cpp
forms/PlanVisualizerPage.ui
python/multi_rig_plan/
  ├── serial.py           # 串行规划脚本
  ├── scheduler.py        # 优化规划脚本
  └── durations_override.json  # 运行时生成的配置
```

#### 关键技术点

1. **QCustomPlot甘特图**
   - 使用QCPItemRect绘制任务条
   - 8个DOF (Fz, Sr, Me, Mg, Mr, Dh, Pr, Cb)，每个DOF独立颜色
   - 红色虚线分隔4个阶段 (A/B/C/D)
   - 横向拖动和缩放支持

2. **滚动条集成**
   - QScrollArea包裹甘特图
   - 独立的QScrollBar控制X轴范围
   - ASCII输出设置NoWrap模式启用水平滚动
   - 可见范围默认50秒，超出自动启用滚动

3. **Python进程调用**
   - QProcess异步执行Python脚本
   - 实时读取stdout/stderr输出
   - 命令行参数：`--n_pipes`, `--zoom`, `--json`, `--dur_config`

4. **项目路径解析**
   ```cpp
   static QString getProjectRoot() {
       QString appDir = QCoreApplication::applicationDirPath();
       return QDir::cleanPath(appDir + "/../..");
   }
   ```
   解决了build/debug目录与项目根目录的路径问题

5. **参数同步机制**
   - 每次运行规划都写入最新时长配置到临时文件
   - 总是传递`--dur_config`参数给Python脚本
   - 确保钻杆数量、缩放、时长的任何修改都能实时生效

#### DOF定义
```cpp
static const QStringList DOFS = {"Fz", "Sr", "Me", "Mg", "Mr", "Dh", "Pr", "Cb"};
// Fz: 进给, Sr: 料仓, Me: 机械手伸缩, Mg: 机械手夹紧
// Mr: 机械手回转, Dh: 对接推杆, Pr: 回转, Cb: 夹紧
```

#### 4个钻进阶段
- **Stage A**: 安装钻具 (Install Drill Tool)
- **Stage B**: 接杆钻进 (Install Rods & Drill)
- **Stage C**: 回收钻杆 (Retrieve Rods)
- **Stage D**: 回收钻具 (Retrieve Tool)

### 依赖配置

#### .pro文件修改
- 移除了`webenginewidgets`和`webchannel`模块
- 添加`printsupport`模块（QCustomPlot依赖）
- 集成QCustomPlot第三方库
- Python集成支持 (C:/Users/YMH/miniconda3)

#### Python环境
- Python路径: `C:/Users/YMH/miniconda3/python.exe`
- 依赖包: `ortools` (用于优化规划)

### 已解决的问题

1. **路径问题**
   - 问题：使用`QDir::currentPath()`导致找不到Python脚本
   - 解决：使用`QCoreApplication::applicationDirPath()`并向上导航

2. **WebEngine连接拒绝**
   - 问题：WebEngine加载本地HTML被拒绝
   - 解决：改用Qt原生QCustomPlot绘制甘特图

3. **参数不同步**
   - 问题：修改钻杆数量或时长后，甘特图不更新
   - 解决：每次运行都写入最新配置并传递给Python

4. **长时间轴显示**
   - 问题：多钻杆时甘特图过长，内容挤在一起
   - 解决：添加横向滚动条，可见范围50秒，支持拖动浏览

### UI布局特点
- 顶部控制栏：模式选择、参数设置、操作按钮
- 左侧（可折叠）：时长配置表格
- 右侧主区域：
  - 上方：QCustomPlot甘特图（带滚动条）
  - 下方：ASCII输出（最大高度300px）
- 底部统计栏：串行时间、优化时间、节省时间、阶段说明

### 待优化项
- [ ] 甘特图任务条可以显示工具提示
- [ ] 支持点击任务条高亮对应操作
- [ ] 更丰富的JSON导出信息
- [ ] 异常处理增强
