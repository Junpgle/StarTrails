QT       += core gui widgets concurrent

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

TARGET = StarTrailsCpp
TEMPLATE = app

# --- OpenCV 配置 (请根据实际路径修改) ---
# Windows 示例:
INCLUDEPATH += D:/opencv/build/include
LIBS += -LD:/opencv/build/x64/vc16/lib -lopencv_world460
#
# Mac/Linux (通常使用 pkg-config):
# CONFIG += link_pkgconfig
# PKGCONFIG += opencv4
# ------------------------------------

SOURCES += \
    MainWindow.cpp \
    main.cpp

HEADERS += \
    MainWindow.h

# 禁用控制台窗口 (发布时)
# CONFIG += windows
