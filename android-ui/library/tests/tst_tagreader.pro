# ./mkfixtures.sh writes fixtures/ (ffmpeg); tst_tagreader fixtures [fuzz rounds, default 40]
# compares readTags/readCover against ffprobe.
QT = core gui
CONFIG += c++14 console warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tst_tagreader
QMAKE_CXXFLAGS += -Werror
SOURCES += tst_tagreader.cpp ../tagreader.cpp ../sensme.cpp
HEADERS += ../tagreader.h ../sensme.h
