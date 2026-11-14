QT += core gui qml quick testlib
CONFIG += c++17 testcase
TARGET = vitals_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/apps.h ../src/cli.h ../src/gpu.h ../src/procfs.h ../src/run.h ../src/monitor.h ../src/processmodel.h ../src/sparkline.h ../src/theme.h ../src/translator.h
SOURCES += vitals_tests.cpp ../src/apps.cpp ../src/cli.cpp ../src/gpu.cpp ../src/procfs.cpp ../src/run.cpp ../src/monitor.cpp ../src/processmodel.cpp ../src/sparkline.cpp \
    ../src/theme.cpp ../src/translator.cpp
RESOURCES += ../src/resources.qrc
