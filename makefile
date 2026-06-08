
CC = gcc
WIN_CC = x86_64-w64-mingw32-gcc
CFLAGS = -std=c11 -Wall -Wextra -O2 \
         -Isrc -Ivendor/lua -Ivendor/ibxm -Ivendor/microtar
LIBS = -lSDL2 -lm

TARGET = yf

LUA_SRC = $(filter-out vendor/lua/lua.c vendor/lua/luac.c vendor/lua/onelua.c, \
              $(wildcard vendor/lua/*.c))
IBXM_SRC = $(wildcard vendor/ibxm/*.c)
TAR_SRC = $(wildcard vendor/microtar/*c)

SRC = $(LUA_SRC) $(IBXM_SRC) $(TAR_SRC) src/mem.c \
	src/audio.c src/config.c src/yfc.c src/vm.c src/main.c 

all: $(TARGET)


$(TARGET): $(SRC)
	$(CC) -o $(TARGET) $(CFLAGS) $(LIBS) $(SRC)
	
windows: $(SRC)
	$(WIN_CC) -o $(TARGET) $(CFLAGS) \
	-static-libgcc -static \
	-luser32 -lgdi32 -lwinmm -limm32 -lole32 \
	-loleaut32 -lversion -luuid -lsetupapi -lshell32 -mwindows \
	$(LIBS) $(SRC)
	
