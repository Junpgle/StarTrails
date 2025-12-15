QT       += core gui widgets concurrent

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

TARGET = StarTrailsCpp
TEMPLATE = app

# --- OpenCV 配置 ---

# 1. 头文件路径 (请确认路径存在)
INCLUDEPATH += D:/opencv/build/include

# 2. 库文件路径
LIBS += -LD:/opencv/build/x64/vc16/lib

# 3. 智能链接：Debug 模式链 debug 库，Release 模式链 release 库
# 请务必根据你实际的文件名修改版本号 (例如 4100, 460, 480)
CONFIG(debug, debug|release) {
    # Debug 模式 (带 d)
    LIBS += -lopencv_world4120d
} else {
    # Release 模式 (不带 d)
    LIBS += -lopencv_world4120
}

SOURCES += \
    MainWindow.cpp \
    main.cpp

HEADERS += \
    MainWindow.h

# 禁用控制台窗口 (发布时)
# CONFIG += windows
