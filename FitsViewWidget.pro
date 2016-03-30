#-------------------------------------------------
#
# Project created by QtCreator 2016-01-21T10:31:09
#
#-------------------------------------------------

QT       += widgets

TARGET = FitsViewWidget
TEMPLATE = lib

QMAKE_CXXFLAGS += -std=c++11
#QMAKE_CXXFLAGS += -std=c++11 -fopenmp
#LIBS += -fopenmp
#QMAKE_LFLAGS += -fopenmp

DEFINES += FITSVIEWWIDGET_LIBRARY

SOURCES += FitsViewWidget.cpp

HEADERS += FitsViewWidget.h\
        fitsviewwidget_global.h

unix {
    target.path = /usr/lib
    INSTALLS += target
}


unix:!macx: LIBS += -L/usr/lib64/ -lcfitsio

INCLUDEPATH += /usr/include
DEPENDPATH += /usr/include
