# tst_sensme MODEL BLOBS prints the SensMe port's view of each blob; see sensme_fuzz.py.
QT = core
CONFIG += c++14 console warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tst_sensme
QMAKE_CXXFLAGS += -Werror
SOURCES += tst_sensme.cpp ../sensme.cpp
HEADERS += ../sensme.h
