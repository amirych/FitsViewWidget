#-------------------------------------------------
#
# Project created by QtCreator 2016-01-21T10:31:09
#
#-------------------------------------------------

QT       += widgets

TARGET = FitsViewWidget
TEMPLATE = lib

DEFINES += FITSVIEWWIDGET_LIBRARY

SOURCES += FitsViewWidget.cpp

HEADERS += FitsViewWidget.h\
        fitsviewwidget_global.h

unix {
    target.path = /usr/lib
    INSTALLS += target
}
