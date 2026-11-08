QT       += core gui widgets

TARGET    = git_pull_tool
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
RC_FILE += git_pull_tool_resource.rc

# 版本资源（.rc）也要 include user/inc/appinfo.h，windres 需要这个搜索路径
RC_INCLUDEPATH += user/inc

# qmake 生成的规则只让 res.o 依赖 .rc 文件本身，appinfo.h 改动不会触发版本资源重建，
# 会导致 exe 属性里的版本号滞后于窗口标题。这里在链接前始终重新生成一次资源对象，
# 保证「改一处、全都一致」。$(RES_FILE) 是 Makefile 里的变量，由 make 展开。
QMAKE_PRE_LINK += $$QMAKE_RC -i $$RC_FILE -o $(RES_FILE) --include-dir=$$PWD --include-dir=$$PWD/user/inc

# 更新记录与说明文档编译进资源（更新说明窗口读取 :/docs/README.zh_CN.md）
RESOURCES += resources.qrc
