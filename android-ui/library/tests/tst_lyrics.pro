# tst_lyrics: LRC and plain lyrics parsing, the .lrc lookup and text decoding.
QT = core
CONFIG += c++14 console warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tst_lyrics
QMAKE_CXXFLAGS += -Werror
SOURCES += tst_lyrics.cpp ../lyrics.cpp
HEADERS += ../lyrics.h
