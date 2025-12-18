# ZMotionDriver 编译错误修复清单

## 问题类别

### 1. const正确性问题 ✅ 已修复接口定义
- [x] IMotionDriver.h - 所有get方法添加const
- [x] ZMotionDriver.h - 所有get方法添加const  
- [ ] ZMotionDriver.cpp - 需要逐个方法添加const实现

### 2. ZMotion API类型不匹配

#### A. 连接API
- [x] `ZAux_OpenEth` - char* vs const char* ✅ 已修复（使用QByteArray）

#### B. 总线初始化
- [x] `ZAux_BusInit` 不存在 ✅ 改为 `ZAux_BusStart`

#### C. Get方法返回类型不匹配
需要将 `float*` 改为 `int*` 的函数：
- [x] `ZAux_Direct_GetAxisEnable` ✅ 已修复
- [ ] `ZAux_Direct_GetAtype`（需int*）
- [ ] `ZAux_Direct_GetIfIdle`（需int*）

#### D. 缺少的API函数
- [ ] `ZAux_Direct_GetCurSpeed` - 可能需要改名为 `ZAux_Direct_GetSpeed` 或 `ZAux_Direct_GetMVel`

### 3. 需要添加const的get方法实现（约15个）

```cpp
// 需要修复的方法列表：
double getTargetPosition(int axis) const        ✅ 已修复
double getActualPosition(int axis) const        ⏳ 待修复
double getSpeed(int axis) const                 ⏳ 待修复
double getActualVelocity(int axis) const        ⏳ 待修复  
double getAcceleration(int axis) const          ⏳ 待修复
double getDeceleration(int axis) const          ⏳ 待修复
int getAxisType(int axis) const                 ⏳ 待修复
double getDAC(int axis) const                   ⏳ 待修复
bool isAxisMoving(int axis) const               ⏳ 待修复
MotorStatus getAxisStatus(int axis) const       ⏳ 待修复
```

## 修复策略

### 方案A：全面修复（推荐）
1. 逐个修复所有get方法的const
2. 修正所有ZMotion API类型不匹配
3. 替换缺失的API函数
4. 验证编译通过

**预计时间**：10-15分钟
**优点**：一次性解决所有问题
**缺点**：需要一些时间

### 方案B：先重组文件结构
1. 创建control/motioncontrol文件夹
2. 移动文件到新位置
3. 更新所有引用
4. 然后再修复编译错误

**优点**：文件结构更清晰
**缺点**：可能需要两次编译验证

## 建议

**建议采用方案A**：先修复所有编译错误，确保代码可以编译通过，然后再进行文件重组。

原因：
1. 文件重组会涉及大量路径修改，如果代码本身有问题会增加调试难度
2. 先确保代码正确性，再优化组织结构
3. 修复这些错误只需10-15分钟

## 下一步

用户需要决定：
- [ ] 继续修复剩余的编译错误（推荐）
- [ ] 先进行文件重组
- [ ] 其他方案
