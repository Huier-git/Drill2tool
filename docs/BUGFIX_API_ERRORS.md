# ZMotion API错误修复报告

## 问题概述
在ZMotionDriver.cpp中使用了多个不存在的ZMotion API函数。

## 错误列表

### 1. ❌ ZAux_BusStart 不存在
**位置**: `ZMotionDriver::initBus()` (line 85)  
**当前代码**:
```cpp
int result = ZAux_BusStart(m_handle);
```

**问题**: ZMotion库中没有 `ZAux_BusStart` 函数

**解决方案选项**:
- A) 删除 `initBus()` 功能（如果不需要总线初始化）
- B) 使用 `ZAux_BusCmd_*` 系列函数（需要slot参数）
- C) 直接使用字符串命令: `ZAux_Execute(handle, "BUS_START", ...)`

**推荐**: 方案A - 通常EtherCAT总线在控制器启动时自动初始化

---

### 2. ❌ ZAux_Direct_GetCurSpeed 不存在  
**位置**: `ZMotionDriver::getActualVelocity()` (line 212)
**当前代码**:
```cpp
int result = ZAux_Direct_GetCurSpeed(m_handle, axis, &value);
```

**问题**: 没有 `ZAux_Direct_GetCurSpeed` 函数

**解决方案选项**:
- A) 使用 `ZAux_Direct_GetSpeed()` - 返回设定速度，不是实际速度
- B) 使用 modbus快速读取: `ZAux_GetModbusCurSpeed()`
- C) 计算速度: (当前DPOS - 上次DPOS) / 时间间隔
- D) 使用字符串命令: `ZAux_DirectCommand(handle, "?VEL(axis)", ...)`

**推荐**: 方案B - 使用 `ZAux_GetModbusCurSpeed()` 批量读取所有轴速度

---

## 已确认正确的API

✅ **ZAux_OpenEth**(char *ipaddr, ZMC_HANDLE *phandle) - 需要char*  
✅ **ZAux_Direct_GetAxisEnable**(handle, axis, int *piValue) - 使用int*  
✅ **ZAux_Direct_GetAtype**(handle, axis, int *piValue) - 使用int*  
✅ **ZAux_Direct_GetMpos**(handle, axis, float *pfValue) - 使用float*  
✅ **ZAux_Direct_GetDpos**(handle, axis, float *pfValue) - 使用float*  
✅ **ZAux_Direct_GetSpeed**(handle, axis, float *pfValue) - 使用float* (设定速度)  
✅ **ZAux_Direct_GetAccel**(handle, axis, float *pfValue) - 使用float*  
✅ **ZAux_Direct_GetDecel**(handle, axis, float *pfValue) - 使用float*  
✅ **ZAux_Direct_GetDAC**(handle, axis, float *fValue) - 使用float*  
✅ **ZAux_Direct_GetIfIdle**(handle, axis, int *piValue) - 使用int*

---

## 修复计划

1. **立即修复const方法**（已进行中）
   - ✅ getAxisEnable() 
   - ✅ getTargetPosition()
   - ✅ getActualPosition()  
   - ⏳ getSpeed()
   - ⏳ getAcceleration()
   - ⏳ getDeceleration()
   - ⏳ getAxisType()
   - ⏳ getDAC()
   - ⏳ isAxisMoving()
   - ⏳ getAxisStatus()

2. **修复不存在的API**
   - ❌ 删除或重写 initBus()
   - ❌ 重写 getActualVelocity() 使用modbus或计算方法

3. **类型修正**
   - ✅ getAxisEnable: float* → int*
   - ⏳ getAxisType: float* → int*  
   - ⏳ isAxisMoving: uint32_t* → int*

---

## 推荐实现

### getActualVelocity() 重写方案

```cpp
double ZMotionDriver::getActualVelocity(int axis) const
{
    QMutexLocker locker(&m_mutex);
    if (!checkConnection()) return 0.0;
    
    // 方案1: 使用modbus批量读取（更高效）
    float velocities[8];
    int result = ZAux_GetModbusCurSpeed(m_handle, axis + 1, velocities);
    if (const_cast<ZMotionDriver*>(this)->checkError(result, QString("GetModbusCurSpeed(%1)").arg(axis))) {
        return static_cast<double>(velocities[axis]);
    }
    
    return 0.0;
}
```

或者

```cpp
double ZMotionDriver::getActualVelocity(int axis) const
{
    QMutexLocker locker(&m_mutex);
    if (!checkConnection()) return 0.0;
    
    // 方案2: 使用字符串命令
    char cmdbuff[64];
    char response[256];
    sprintf(cmdbuff, "?VEL(%d)", axis);
    
    int result = ZAux_DirectCommand(m_handle, cmdbuff, response, sizeof(response));
    if (const_cast<ZMotionDriver*>(this)->checkError(result, QString("?VEL(%1)").arg(axis))) {
        return atof(response);
    }
    
    return 0.0;
}
```

### initBus() 重写方案

```cpp
bool ZMotionDriver::initBus()
{
    QMutexLocker locker(&m_mutex);
    if (!checkConnection()) return false;

    // 大多数情况下EtherCAT总线自动初始化，不需要显式调用
    // 如果需要，使用字符串命令
    char response[256];
    int result = ZAux_Execute(m_handle, "BUS_START", 5000, response, sizeof(response));
    
    if (!checkError(result, "BUS_START")) {
        return false;
    }

    qDebug() << "[ZMotionDriver] Bus initialized";
    emit commandExecuted("Bus initialized");
    return true;
}
```

或者简单返回true:

```cpp
bool ZMotionDriver::initBus()
{
    // EtherCAT总线在控制器启动时自动初始化
    qDebug() << "[ZMotionDriver] Bus auto-initialized by controller";
    return true;
}
```
