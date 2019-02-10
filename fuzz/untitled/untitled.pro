TEMPLATE = app
TARGET = fuzz-ctelnet
INCLUDEPATH += .

QT += network opengl uitools multimedia gui concurrent

QMAKE_CXXFLAGS += -fsanitize=address,fuzzer
QMAKE_LFLAGS += -fsanitize=address,fuzzer

        LIBS += \
            -llua5.1 \
            -lhunspell
        INCLUDEPATH += /usr/include/lua5.1

    LIBS += -lpcre \
        -L/usr/local/lib/ \
        -lyajl \
        -lGLU \
        -lzip \
        -lz \
        -lpugixml


SOURCES += main.cpp

include(../../src/mudlet.pro)
