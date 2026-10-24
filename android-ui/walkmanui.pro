QT += quick qml
CONFIG += c++14
QMAKE_CXXFLAGS += -Werror
TEMPLATE = app
TARGET = walkmanui

# The Sony views, this app's QML and the screen tables are not compiled in:
# stage.py packs them into build/res (APK assets on Android), never committed.
DEFINES += WALKMANUI_RES_DIR=\\\"$$PWD/build/res\\\"

SOURCES += \
    src/appcontext.cpp \
    src/backkey.cpp \
    src/coverart.cpp \
    src/dialogmanager.cpp \
    src/main.cpp \
    src/rowmodel.cpp \
    src/screencontroller.cpp \
    src/screensettings.cpp \
    src/signalrelay.cpp \
    src/soundeffects.cpp \
    src/swipegesture.cpp \
    src/testdriver.cpp \
    src/models/browsemodel.cpp \
    src/models/choicelist.cpp \
    src/models/librarytopmodel.cpp \
    src/models/playermodel.cpp \
    src/models/popupmodels.cpp \
    src/models/queuemodels.cpp \
    src/models/screenmodel.cpp \
    src/models/sensmemodel.cpp \
    src/models/settingsmodels.cpp \
    src/models/soundmodels.cpp \
    src/models/soundstate.cpp \
    src/models/windowmodel.cpp

HEADERS += \
    src/appcontext.h \
    src/backkey.h \
    src/coverart.h \
    src/dialogmanager.h \
    src/inputcontextproxy.h \
    src/rowmodel.h \
    src/screencontroller.h \
    src/screensettings.h \
    src/signalrelay.h \
    src/soundeffects.h \
    src/swipegesture.h \
    src/testdriver.h \
    src/models/browsemodel.h \
    src/models/choicelist.h \
    src/models/librarytopmodel.h \
    src/models/playermodel.h \
    src/models/popupmodels.h \
    src/models/queuemodels.h \
    src/models/screenmodel.h \
    src/models/sensmemodel.h \
    src/models/settingsmodels.h \
    src/models/soundmodels.h \
    src/models/soundstate.h \
    src/models/windowmodel.h

HEADERS += audiofx/fxsettings.h

include(library/library.pri)
include(playback/playback.pri)
android: ANDROID_PACKAGE_SOURCE_DIR = $$PWD/build/android-pkg
