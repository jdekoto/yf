
CC = gcc
WIN_CC = x86_64-w64-mingw32-gcc
CFLAGS = -std=c11 -Wall -Wextra -O2 \
         -Isrc -Ivendor/lua -Ivendor/micromod -Ivendor/microtar \
         
LIBS = -lSDL2 -lm

WIN_FLAGS = -Ivendor/sdl2/include

WIN_LDF =-Lvendor/sdl2/lib -lmingw32 -lSDL2main -lSDL2 -mwindows \
	-luser32 -lgdi32 -lwinmm -limm32 -lole32 \
	-loleaut32 -lversion -luuid -lsetupapi -lshell32 \
	-static-libgcc -static \
	
TARGET = yf

LUA_SRC = $(filter-out vendor/lua/lua.c vendor/lua/luac.c vendor/lua/onelua.c, \
              $(wildcard vendor/lua/*.c))
MMOD_SRC = $(wildcard vendor/micromod/*.c)
TAR_SRC = $(wildcard vendor/microtar/*c)

SRC = $(LUA_SRC) $(MMOD_SRC) $(TAR_SRC) src/mem.c \
	src/audio.c src/config.c src/yfc.c src/api.c src/vm.c src/main.c 

all: $(TARGET)


$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) $(LIBS) -o $(TARGET)
	
windows: $(SRC)
	$(WIN_CC) $(CFLAGS) $(WIN_FLAGS) $(SRC) $(WIN_LDF) -o $(TARGET)
	

