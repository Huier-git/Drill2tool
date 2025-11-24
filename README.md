# DrillControl 钻机采集控制系统 v2.0

## 项目简介

这是一个基于Qt5的钻机数据采集与控制上位机软件，采用简洁的模块化设计。

## 项目结构

```
DrillControl/
├── DrillControl.pro              # Qt项目文件
├── src/                          # 源代码目录
│   ├── main.cpp                  # 程序入口
│   ├── ui/                       # 界面层
│   │   └── MainWindow.cpp        # 主窗口实现
│   ├── data/                     # 数据采集层（待实现）
│   │   ├── VibrationCollector.cpp  # 震动传感器采集
│   │   ├── MdbCollector.cpp        # MDB传感器采集
│   │   └── MotorParamReader.cpp    # 电机参数读取
│   ├── database/                 # 数据库层（待实现）
│   │   └── DatabaseManager.cpp
│   └── control/                  # 控制层（待实现）
│       └── DrillController.cpp
├── include/                      # 头文件目录
│   ├── ui/                       # 界面头文件
│   ├── data/                     # 数据采集头文件
│   ├── database/                 # 数据库头文件
│   └── control/                  # 控制头文件
├── forms/                        # Qt UI文件
│   └── MainWindow.ui             # 主窗口界面
├── thirdparty/                   # 第三方库
│   ├── qcustomplot/              # 图表库
│   ├── vk701/                    # VK701采集卡
│   ├── zmotion/                  # ZMotion运动控制
│   └── sqlite3/                  # SQLite数据库
├── python/                       # Python脚本集成
├── config/                       # 配置文件
├── database/                     # 数据库文件
└── resources/                    # 资源文件

```

## 功能模块

### 1. 数据采集模块（待实现）

- **震动传感器采集**: VK701采集卡，3通道，5000Hz可调
- **MDB数据采集**: 上下拉力、位置、扭矩传感器，10Hz
- **电机参数读取**: 三环参数读取

### 2. 界面功能（已完成基础框架）

- **多页面管理**: 侧边栏标签切换
- **工业级布局**: 简洁的UI设计
- **子界面**:
  - 数据采集页面
  - 钻机控制页面
  - 数据库管理页面

### 3. 控制功能（待实现）

- 钻机多自由度控制界面

### 4. 数据库功能（待实现）

- SQLite3异步批量写入
- 数据查询与管理界面

## 技术特点

- **多线程数据采集**: 不阻塞主控制线程
- **异步数据库写入**: 高频数据采集性能优化
- **模块化设计**: 清晰的目录结构和职责分离
- **Python集成支持**: 预留Python脚本接口

## 开发环境

- **Qt版本**: Qt 5.15.2
- **编译器**: MSVC 2019 / MinGW 8.1
- **C++标准**: C++17
- **操作系统**: Windows 10/11

## 编译说明

### 使用Qt Creator

1. 打开 `DrillControl.pro`
2. 配置构建工具（MSVC或MinGW）
3. 点击构建按钮

### 使用命令行

```bash
# 生成Makefile
qmake DrillControl.pro

# 编译
nmake          # MSVC
mingw32-make   # MinGW

# 运行
cd build/debug
DrillControl.exe
```

## 第三方库依赖

### QCustomPlot
- 用途: 实时数据绘图
- 版本: 2.x
- 位置: `thirdparty/qcustomplot/`

### VK701采集卡
- 用途: 震动传感器数据采集
- 位置: `thirdparty/vk701/`
- DLL: `VK70xNMC_DAQ2.dll`

### ZMotion
- 用途: 运动控制
- 位置: `thirdparty/zmotion/`
- DLL: `zmotion.dll`, `zauxdll.dll`

### SQLite3
- 用途: 本地数据库
- 位置: `thirdparty/sqlite3/`
- DLL: `sqlite3.dll`

## 待实现功能

- [ ] 震动传感器采集模块
- [ ] MDB传感器采集模块
- [ ] 电机参数读取模块
- [ ] 数据库管理器
- [ ] 钻机控制器
- [ ] 数据查询界面
- [ ] 数据可视化图表
- [ ] Python脚本集成

## 项目特色

### 相比旧版本的改进

1. **目录结构简洁**: 按功能模块清晰分层
2. **第三方库集中管理**: 所有依赖统一放在 thirdparty 目录
3. **易于维护**: 模块化设计，职责明确
4. **预留扩展接口**: Python集成、配置文件等
5. **遵循KISS原则**: 简单可维护，不过度设计

## 开发计划

### 第一阶段: 基础框架 ✅
- [x] 项目目录结构
- [x] 主窗口框架
- [x] 页面切换机制

### 第二阶段: 数据采集
- [ ] VK701震动采集
- [ ] MDB传感器采集
- [ ] 电机参数读取

### 第三阶段: 数据库
- [ ] SQLite数据库封装
- [ ] 异步写入机制
- [ ] 数据查询界面

### 第四阶段: 控制功能
- [ ] 运动控制封装
- [ ] 控制界面开发
- [ ] 多自由度控制

## 许可证

内部项目

## 联系方式

开发团队: KT团队
