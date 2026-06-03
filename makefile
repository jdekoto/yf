
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -O2 \
         -Isrc -Ivendor/lua
LIBS = -lSDL2 -lm

TARGET = yf

LUA_SRC = $(filter-out vendor/lua/lua.c vendor/lua/luac.c vendor/lua/onelua.c, \
              $(wildcard vendor/lua/*.c))

SRC = $(LUA_SRC) src/mem.c \
	src/audio.c src/config.c src/vm.c src/main.c 

all: $(TARGET)


$(TARGET): $(SRC)
	$(CC) -o $(TARGET) $(CFLAGS) $(LIBS) $(SRC)
	
	
