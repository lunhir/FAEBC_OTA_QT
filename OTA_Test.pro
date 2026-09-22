QT += core gui widgets sql network serialport

CONFIG += c++17

# 把 .pro 所在目录注入为编译期宏，数据库固定存放在项目源码目录，
# 与工作目录/Debug/Release 无关（修复 Debug 下打开旧只读库的问题）
DEFINES += APP_SOURCE_DIR=\\\"$$PWD\\\"

QMAKE_CXX  = D:/Qt/Tools/mingw1310_64/bin/g++.exe
QMAKE_CC   = D:/Qt/Tools/mingw1310_64/bin/gcc.exe
QMAKE_LINK = D:/Qt/Tools/mingw1310_64/bin/g++.exe

SOURCES += \
    main.cpp \
    widget.cpp \
    protocol.cpp \
    mqttclient.cpp \
    mqttotatask.cpp \
    lockeventspage.cpp

HEADERS += \
    widget.h \
    protocol.h \
    mqttclient.h \
    mqttotatask.h \
    lockeventspage.h

FORMS += \
    widget.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

SOURCES += systemdatapage.cpp
HEADERS += systemdatapage.h
