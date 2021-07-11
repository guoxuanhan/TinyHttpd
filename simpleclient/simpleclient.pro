QT += core
QT -= gui

CONFIG += c++11
QMAKE_CXXFLAGS = -std=c++0x
TARGET = simpleclient
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES += \
    simpleclient.cpp
