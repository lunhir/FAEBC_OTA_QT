QT += core gui widgets sql network serialport

CONFIG += c++17

QMAKE_CXX  = D:/Qt/Tools/mingw1310_64/bin/g++.exe
QMAKE_CC   = D:/Qt/Tools/mingw1310_64/bin/gcc.exe
QMAKE_LINK = D:/Qt/Tools/mingw1310_64/bin/g++.exe

SOURCES += \
    main.cpp \
    widget.cpp \
    protocol.cpp \
    mqttclient.cpp

HEADERS += \
    widget.h \
    protocol.h \
    mqttclient.h

FORMS += \
    widget.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
