TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

QMAKE_CXXFLAGS_RELEASE *= -O3

SOURCES += main.cpp

HEADERS += \
    binstreamwrap.hpp \
    binstreamwrapfwd.hpp \
    hash_file_storage.hpp

LIBPATH += /usr/local/lib/
LIBS += $${LIBPATH}libboost_system.a \
    $${LIBPATH}libboost_filesystem.a
