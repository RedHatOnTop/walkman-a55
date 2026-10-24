TEMPLATE = app
TARGET = hosttest
QT += quick qml network
CONFIG += c++11
INCLUDEPATH += ../mkspec
SOURCES += main.cpp ../mkspec/overlay.cpp ../mkspec/probe.cpp
HEADERS += ../mkspec/overlay.h ../mkspec/probe.h
RESOURCES += hosttest.qrc
# The overlay handles rcc format 1 only, which is all Qt 5.3 writes.
greaterThan(QT_MINOR_VERSION, 7): QMAKE_RESOURCE_FLAGS += --format-version 1
