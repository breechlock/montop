QT += core gui qml quick quickcontrols2

CONFIG += c++17 release
TARGET = vitals
TEMPLATE = app

# The one place the version is set; a release also sets pkgver in the PKGBUILDs.
VERSION = 0.3.1
DEFINES += VITALS_VERSION=\\\"$$VERSION\\\"

HEADERS += \
    src/apps.h \
    src/cli.h \
    src/procfs.h \
    src/run.h \
    src/monitor.h \
    src/processmodel.h \
    src/sparkline.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/apps.cpp \
    src/cli.cpp \
    src/run.cpp \
    src/procfs.cpp \
    src/monitor.cpp \
    src/processmodel.cpp \
    src/sparkline.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
