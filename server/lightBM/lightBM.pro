#-------------------------------------------------
#
# Project created by QtCreator 2018-06-20T17:43:21
#
#-------------------------------------------------

QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = lightBM
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

INCLUDEPATH += /System/Library/Frameworks/Python.framework/Versions/2.7/include/python2.7
#LIBS += -lpython2.7

SOURCES += \
        main.cpp \
        mainwindow.cpp \
        application.cpp \
    myclient.cpp \
    myriddle.cpp \
    myserver.cpp \
    mytask.cpp \
#    pyrun.cpp \
    dialog.cpp

HEADERS += \
        mainwindow.h \
        application.h \
    myclient.h \
    myriddle.h \
    myserver.h \
    mytask.h \
#    pyrun.h \
    dialog.h

FORMS += \
        mainwindow.ui \
    dialog.ui

RESOURCES += \
    qdarkstyle/style.qrc \
    media.qrc \
    include.qrc

DISTFILES +=
