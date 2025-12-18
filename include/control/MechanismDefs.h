#ifndef MECHANISMDEFS_H
#define MECHANISMDEFS_H

#include <QString>
#include <QMap>

/**
 * @file MechanismDefs.h
 * @brief 钻机机构代号和常量定义
 *
 * 定义钻机系统中所有机构的代号、电机索引映射和相关常量
 *
 * 机构命名规范:
 * - Fz: 进给机构 (Feed) - 动力箱上下移动
 * - Sr: 存储机构 (Storage) - 7位置转盘
 * - Me: 机械手伸缩 (Manipulator Extension)
 * - Mg: 机械手夹紧 (Manipulator Grip)
 * - Mr: 机械手旋转 (Manipulator Rotation)
 * - Dh: 对接头 (Docking Head) - Modbus推杆
 * - Pr: 回转 (Power Rotation) - 钻管旋转
 * - Pi: 冲击 (Percussion Impact)
 * - Cb: 下夹紧 (Clamp Bottom)
 */

namespace Mechanism {

// ============================================================================
// 机构代号枚举
// ============================================================================

/**
 * @brief 机构代号枚举
 */
enum Code {
    Fz = 0,     // 进给机构
    Sr = 1,     // 存储机构
    Me = 2,     // 机械手伸缩
    Mg = 3,     // 机械手夹紧
    Mr = 4,     // 机械手旋转
    Dh = 5,     // 对接头
    Pr = 6,     // 回转
    Pi = 7,     // 冲击
    Cb = 8,     // 下夹紧
    COUNT = 9   // 机构总数
};

// ============================================================================
// 电机索引映射
// ============================================================================

/**
 * @brief 机构代号到电机索引的映射
 * -1 表示非EtherCAT电机（如Modbus设备）
 */
constexpr int MotorIndex[COUNT] = {
    2,      // Fz -> EtherCAT轴2
    7,      // Sr -> EtherCAT轴7
    6,      // Me -> EtherCAT轴6
    4,      // Mg -> EtherCAT轴4
    5,      // Mr -> EtherCAT轴5
    -1,     // Dh -> Modbus (非EtherCAT)
    0,      // Pr -> EtherCAT轴0
    1,      // Pi -> EtherCAT轴1
    3       // Cb -> EtherCAT轴3
};

// ============================================================================
// 机构名称定义
// ============================================================================

/**
 * @brief 机构代号字符串
 */
constexpr const char* CodeStr[COUNT] = {
    "Fz",   // 进给
    "Sr",   // 存储
    "Me",   // 机械手伸缩
    "Mg",   // 机械手夹紧
    "Mr",   // 机械手旋转
    "Dh",   // 对接头
    "Pr",   // 回转
    "Pi",   // 冲击
    "Cb"    // 下夹紧
};

/**
 * @brief 机构中文名称
 */
constexpr const char* NameCN[COUNT] = {
    "进给机构",
    "存储机构",
    "机械手伸缩",
    "机械手夹紧",
    "机械手旋转",
    "对接头",
    "回转",
    "冲击",
    "下夹紧"
};

/**
 * @brief 机构英文名称
 */
constexpr const char* NameEN[COUNT] = {
    "Feed",
    "Storage",
    "Arm Extension",
    "Arm Grip",
    "Arm Rotation",
    "Docking",
    "Rotation",
    "Percussion",
    "Clamp"
};

// ============================================================================
// 辅助函数
// ============================================================================

/**
 * @brief 根据代号获取电机索引
 * @param code 机构代号
 * @return 电机索引，-1表示非EtherCAT或无效代号
 */
inline int getMotorIndex(Code code) {
    if (code >= 0 && code < COUNT) {
        return MotorIndex[code];
    }
    return -1;
}

/**
 * @brief 根据代号获取代号字符串
 * @param code 机构代号
 * @return 代号字符串
 */
inline QString getCodeString(Code code) {
    if (code >= 0 && code < COUNT) {
        return QString::fromLatin1(CodeStr[code]);
    }
    return QString();
}

/**
 * @brief 根据代号获取中文名称
 * @param code 机构代号
 * @return 中文名称
 */
inline QString getNameCN(Code code) {
    if (code >= 0 && code < COUNT) {
        return QString::fromUtf8(NameCN[code]);
    }
    return QString();
}

/**
 * @brief 根据代号获取英文名称
 * @param code 机构代号
 * @return 英文名称
 */
inline QString getNameEN(Code code) {
    if (code >= 0 && code < COUNT) {
        return QString::fromLatin1(NameEN[code]);
    }
    return QString();
}

/**
 * @brief 根据代号字符串获取机构代号
 * @param codeStr 代号字符串
 * @return 机构代号，无效返回-1
 */
inline Code fromCodeString(const QString& codeStr) {
    for (int i = 0; i < COUNT; ++i) {
        if (codeStr == CodeStr[i]) {
            return static_cast<Code>(i);
        }
    }
    return static_cast<Code>(-1);
}

/**
 * @brief 根据电机索引获取机构代号
 * @param motorIndex 电机索引
 * @return 机构代号，未找到返回-1
 */
inline Code fromMotorIndex(int motorIndex) {
    for (int i = 0; i < COUNT; ++i) {
        if (MotorIndex[i] == motorIndex) {
            return static_cast<Code>(i);
        }
    }
    return static_cast<Code>(-1);
}

/**
 * @brief 检查机构是否为EtherCAT控制
 * @param code 机构代号
 * @return true表示EtherCAT控制
 */
inline bool isEtherCAT(Code code) {
    return getMotorIndex(code) >= 0;
}

/**
 * @brief 检查机构是否为Modbus控制
 * @param code 机构代号
 * @return true表示Modbus控制
 */
inline bool isModbus(Code code) {
    return code == Dh;  // 目前只有对接头是Modbus
}

// ============================================================================
// 电机控制模式常量
// ============================================================================

/**
 * @brief 电机控制模式
 */
namespace Mode {
    constexpr int Position = 65;    // 位置模式
    constexpr int Velocity = 66;    // 速度模式
    constexpr int Torque = 67;      // 力矩模式
}

// ============================================================================
// 默认控制模式映射
// ============================================================================

/**
 * @brief 各机构默认控制模式
 */
constexpr int DefaultMode[COUNT] = {
    Mode::Position,     // Fz - 位置模式
    Mode::Position,     // Sr - 位置模式
    Mode::Position,     // Me - 位置模式
    Mode::Torque,       // Mg - 力矩模式
    Mode::Position,     // Mr - 位置模式
    -1,                 // Dh - Modbus (不适用)
    Mode::Velocity,     // Pr - 速度模式
    Mode::Velocity,     // Pi - 速度模式
    Mode::Torque        // Cb - 力矩模式
};

/**
 * @brief 获取机构默认控制模式
 */
inline int getDefaultMode(Code code) {
    if (code >= 0 && code < COUNT) {
        return DefaultMode[code];
    }
    return -1;
}

} // namespace Mechanism

#endif // MECHANISMDEFS_H
