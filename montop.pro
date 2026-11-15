QT += core gui qml quick quickcontrols2

CONFIG += c++17 release
TARGET = montop
TEMPLATE = app

# The one place the version is set; a release also sets pkgver in the PKGBUILDs.
# It reaches the code through a generated header rather than a define, so
# changing it rebuilds what uses it.
VERSION = 0.5.0
VERSION_HEADER = "$${LITERAL_HASH}pragma once" "$${LITERAL_HASH}define MONTOP_VERSION \"$$VERSION\""
write_file($$OUT_PWD/version.h, VERSION_HEADER)
INCLUDEPATH += $$OUT_PWD

HEADERS += \
    src/apps.h \
    src/cli.h \
    src/gpu.h \
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
    src/gpu.cpp \
    src/run.cpp \
    src/procfs.cpp \
    src/monitor.cpp \
    src/processmodel.cpp \
    src/sparkline.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
