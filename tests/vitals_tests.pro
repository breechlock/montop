QT += core gui qml quick testlib
CONFIG += c++17 testcase
TARGET = vitals_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/procfs.h ../src/monitor.h ../src/processmodel.h ../src/sparkline.h ../src/theme.h ../src/translator.h
SOURCES += vitals_tests.cpp ../src/procfs.cpp ../src/monitor.cpp ../src/processmodel.cpp ../src/sparkline.cpp \
    ../src/theme.cpp ../src/translator.cpp
RESOURCES += ../src/resources.qrc
