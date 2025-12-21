# ==================================================
# DrillControl - 钻机采集控制上位机
# ==================================================

QT += core gui widgets sql network concurrent serialbus printsupport

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
    src/Global.cpp \
    src/Logger.cpp \
    src/ui/MainWindow.cpp \
    src/ui/SensorPage.cpp \
    src/ui/VibrationPage.cpp \
    src/ui/MdbPage.cpp \
    src/ui/MotorPage.cpp \
    src/ui/ControlPage.cpp \
    src/ui/DatabasePage.cpp \
    src/ui/DrillControlPage.cpp \
    src/ui/PlanVisualizerPage.cpp \
    src/dataACQ/BaseWorker.cpp \
    src/dataACQ/VibrationWorker.cpp \
    src/dataACQ/MdbWorker.cpp \
    src/dataACQ/MotorWorker.cpp \
    src/database/DbWriter.cpp \
    src/database/DataQuerier.cpp \
    src/control/AcquisitionManager.cpp \
    src/control/MotionLockManager.cpp \
    src/control/MotionConfigManager.cpp \
    src/control/MechanismTypes.cpp \
    src/control/UnitConverter.cpp \
    src/control/ZMotionDriver.cpp \
    src/control/BaseMechanismController.cpp \
    src/control/StorageController.cpp \
    src/control/ClampController.cpp \
    src/control/FeedController.cpp \
    src/control/RotationController.cpp \
    src/control/PercussionController.cpp \
    src/control/ArmExtensionController.cpp \
    src/control/ArmGripController.cpp \
    src/control/ArmRotationController.cpp \
    src/control/DockingController.cpp \
    src/control/DrillParameterPreset.cpp \
    src/control/SafetyWatchdog.cpp \
    src/control/AutoDrillManager.cpp \
    src/ui/AutoTaskPage.cpp

# ==================================================
# 头文件
# ==================================================
HEADERS += \
    include/Global.h \
    include/Logger.h \
    include/ui/MainWindow.h \
    include/ui/SensorPage.h \
    include/ui/VibrationPage.h \
    include/ui/MdbPage.h \
    include/ui/MotorPage.h \
    include/ui/ControlPage.h \
    include/ui/DatabasePage.h \
    include/dataACQ/DataTypes.h \
    include/dataACQ/BaseWorker.h \
    include/dataACQ/VibrationWorker.h \
    include/dataACQ/MdbWorker.h \
    include/dataACQ/MotorWorker.h \
    include/database/DbWriter.h \
    include/database/DataQuerier.h \
    include/control/AcquisitionManager.h \
    include/control/MotionLockManager.h \
    include/control/MotionConfigManager.h \
    include/control/MechanismDefs.h \
    include/control/zmotion.h \
    include/control/zmcaux.h \
    include/control/MechanismTypes.h \
    include/control/UnitConverter.h \
    include/control/IMotionDriver.h \
    include/control/ZMotionDriver.h \
    include/control/BaseMechanismController.h \
    include/control/StorageController.h \
    include/control/ClampController.h \
    include/control/FeedController.h \
    include/control/RotationController.h \
    include/control/PercussionController.h \
    include/control/ArmExtensionController.h \
    include/control/ArmGripController.h \
    include/control/ArmRotationController.h \
    include/control/DockingController.h \
    include/control/DrillParameterPreset.h \
    include/control/SafetyWatchdog.h \
    include/control/AutoDrillManager.h \
    include/ui/DrillControlPage.h \
    include/ui/PlanVisualizerPage.h \
    include/ui/AutoTaskPage.h

# ==================================================
# UI界面文件
# ==================================================
FORMS += \
    $$PWD/forms/MainWindow.ui \
    $$PWD/forms/SensorPage.ui \
    $$PWD/forms/VibrationPage.ui \
    $$PWD/forms/MdbPage.ui \
    $$PWD/forms/MotorPage.ui \
    $$PWD/forms/ControlPage.ui \
    $$PWD/forms/DatabasePage.ui \
    $$PWD/forms/DrillControlPage.ui \
    $$PWD/forms/PlanVisualizerPage.ui \
    $$PWD/forms/AutoTaskPage.ui

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
# 资源文件
# ==================================================
RESOURCES += resources/resources.qrc

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

# ZMotion运动控制
INCLUDEPATH += $$PWD/thirdparty/zmotion/include
win32: LIBS += -L$$PWD/thirdparty/zmotion/lib -lzmotion -lzauxdll

# SQLite3 (Qt SQL模块已包含SQLite支持，无需额外链接)
# INCLUDEPATH += $$PWD/thirdparty/sqlite3/include
# win32: LIBS += -L$$PWD/thirdparty/sqlite3/lib -lsqlite3

# ==================================================
# Windows平台编译选项
# ==================================================
win32 {
    # MSVC编译器
    msvc {
        QMAKE_CXXFLAGS += /utf-8 /wd4828
    }
    
    # MinGW编译器
    *-g++* {
        QMAKE_CXXFLAGS += -fpermissive
        QMAKE_LFLAGS += -static-libgcc -static-libstdc++
    }
}

# ==================================================
# Python集成支持
# ==================================================
win32 {
    INCLUDEPATH += C:/Users/YMH/miniconda3/include
    LIBS += -LC:/Users/YMH/miniconda3/libs -lpython313
}

# ==================================================
# 部署规则 - 自动复制DLL到输出目录
# ==================================================
win32 {
    # 部署脚本将在实现功能后添加
}
