######################################################################
# Automatically generated by qmake (3.0) Sun Jan 7 20:11:09 2018
######################################################################

TEMPLATE = app
CONFIG += console c++14 thread
CONFIG -= qt app_bundle
TARGET = mexfw
INCLUDEPATH += . \
               ./thirdparty/elle/src \
               ./thirdparty/elle/_build/linux64/boost/1.60.0/include \
               ./thirdparty/rapidjson/include \

LIBS += ./thirdparty/elle/_build/linux64/lib/libelle_core.so \
        ./thirdparty/elle/_build/linux64/lib/libelle_reactor.so \
        ./thirdparty/elle/_build/linux64/lib/libelle_protocol.so \
        ./thirdparty/elle/_build/linux64/lib/libfuse.so.2
LIBS += ./thirdparty/elle/_build/linux64/lib/libboost_*

# Input
HEADERS += *.hpp 

SOURCES += main.cpp 

