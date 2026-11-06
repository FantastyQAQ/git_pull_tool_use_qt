QT       += core gui widgets concurrent

TARGET    = github_pull_tool
TEMPLATE  = app
CONFIG   += c++17

# 源码与头文件统一放在 user 目录下
SOURCES += user/src/main.cpp \
           user/src/mainwindow.cpp \
           user/src/gitmanager.cpp \
           user/src/aboutdialog.cpp \
           user/src/updatenotesdialog.cpp

HEADERS += user/inc/mainwindow.h \
           user/inc/gitmanager.h \
           user/inc/appinfo.h \
           user/inc/aboutdialog.h \
           user/inc/updatenotesdialog.h

# 头文件搜索路径（main.cpp 中 #include "mainwindow.h" 依赖此配置）
INCLUDEPATH += user/inc

# 应用程序图标（编译进 exe，窗口/任务栏图标自动继承）
RC_FILE += github_pull_tool_resource.rc

# README.md 编译进资源（「更新说明」窗口读取 :/docs/README.md 显示更新记录）
RESOURCES += resources.qrc
