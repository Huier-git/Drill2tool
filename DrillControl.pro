# ==================================================
# DrillControl - 钻机采集控制上位机
# ==================================================

QT += core gui widgets sql network concurrent serialbus

CONFIG += c++17

TARGET = DrillControl
TEMPLATE = app

# ==================================================
# 编译输出目录配置
# ==================================================
CONFIG(debug, debug|release) {
    DESTDIR = $$PWD/build/debug
    OBJECTS_DIR = $$PWD/build/debug/obj
    MOC_DIR = $$PWD/build/debug/moc
    RCC_DIR = $$PWD/build/debug/rcc
    UI_DIR = $$PWD/build/debug/ui
} else {
    DESTDIR = $$PWD/build/release
    OBJECTS_DIR = $$PWD/build/release/obj
    MOC_DIR = $$PWD/build/release/moc
    RCC_DIR = $$PWD/build/release/rcc
    UI_DIR = $$PWD/build/release/ui
}

# ==================================================
# 源文件
# ==================================================
SOURCES += \
    src/main.cpp \
    src/ui/MainWindow.cpp \
    src/ui/SensorPage.cpp \
    src/dataACQ/BaseWorker.cpp \
    src/dataACQ/VibrationWorker.cpp \
    src/dataACQ/MdbWorker.cpp \
    src/dataACQ/MotorWorker.cpp \
    src/database/DbWriter.cpp \
    src/control/AcquisitionManager.cpp

# ==================================================
# 头文件
# ==================================================
HEADERS += \
    include/ui/MainWindow.h \
    include/ui/SensorPage.h \
    include/dataACQ/DataTypes.h \
    include/dataACQ/BaseWorker.h \
    include/dataACQ/VibrationWorker.h \
    include/dataACQ/MdbWorker.h \
    include/dataACQ/MotorWorker.h \
    include/database/DbWriter.h \
    include/control/AcquisitionManager.h

# ==================================================
# UI界面文件
# ==================================================
FORMS += \
    $$PWD/forms/MainWindow.ui \
    $$PWD/forms/SensorPage.ui

# ==================================================
# 头文件路径
# ==================================================
INCLUDEPATH += \
    $$PWD/include \
    $$PWD/include/ui \
    $$PWD/include/dataACQ \
    $$PWD/include/database \
    $$PWD/include/control

# ==================================================
# 第三方库配置
# ==================================================

# QCustomPlot
INCLUDEPATH += $$PWD/thirdparty/qcustomplot/include
SOURCES += $$PWD/thirdparty/qcustomplot/src/qcustomplot.cpp
HEADERS += $$PWD/thirdparty/qcustomplot/include/qcustomplot.h

# VK701采集卡
INCLUDEPATH += $$PWD/thirdparty/vk701/include
win32: LIBS += -L$$PWD/thirdparty/vk701/lib -lVK70XNMC_DAQ2

# ZMotion运动控制（暂时注释，实现Worker时再启用）
# INCLUDEPATH += $$PWD/thirdparty/zmotion/include
# win32: LIBS += -L$$PWD/thirdparty/zmotion/lib -lzmotion -lzauxdll

# SQLite3 (Qt SQL模块已包含SQLite支持，无需额外链接)
# INCLUDEPATH += $$PWD/thirdparty/sqlite3/include
# win32: LIBS += -L$$PWD/thirdparty/sqlite3/lib -lsqlite3

# ==================================================
# Windows平台编译选项
# ==================================================
win32 {
    # MSVC编译器
    msvc {
        QMAKE_CXXFLAGS += /utf-8
    }
    
    # MinGW编译器
    *-g++* {
        QMAKE_CXXFLAGS += -fpermissive
        QMAKE_LFLAGS += -static-libgcc -static-libstdc++
    }
}

# ==================================================
# Python集成支持（暂时注释，后续实现时启用）
# ==================================================
# win32 {
#     INCLUDEPATH += C:/Users/YMH/miniconda3/include
#     LIBS += -LC:/Users/YMH/miniconda3/libs -lpython313
# }

# ==================================================
# 部署规则 - 自动复制DLL到输出目录
# ==================================================
win32 {
    # 部署脚本将在实现功能后添加
}
