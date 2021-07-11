QT += core
QT -= gui

CONFIG += c++11
QMAKE_CXXFLAGS = -std=c++0x
TARGET = TinyHttpd
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES += \
    httpd.cpp

HEADERS += \
    threadpool.h
