
CC = clang
WINCC = x86_64-w64-mingw32-gcc
CFLAGS = -std=c11 -Wall -Wextra -O3 -flto \
         -Isrc -Ivendor/lua \
         
LIBS = -lSDL2 -lm

WIN_LDF = -lmingw32 -lSDL2main -lSDL2 -mwindows -lkernel32 \

OSX_LDF = -I./build/osx/YellowFeather.app/Contents/Frameworks/SDL2.framework/Headers \
   	 -F./build/osx/YellowFeather.app/Contents/Frameworks -framework SDL2 \
   	 -O2 -Wall \
   	 -Wl,-rpath,@executable_path/../Frameworks \
   	 -Wl,-rpath,/var/home/dytu/.osxcross/lib \
   	 -arch x86_64 -arch arm64 \
	
TARGET = yf

LUA_SRC = $(filter-out vendor/lua/lua.c vendor/lua/luac.c vendor/lua/onelua.c, \
              $(wildcard vendor/lua/*.c))
              
SRC = $(LUA_SRC) $(MMOD_SRC) $(TAR_SRC) src/mem.c \
	src/audio.c src/config.c src/yfc.c src/api.c src/vm.c src/main.c 

all: $(TARGET)


$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -DLUA_USE_POSIX -D_POSIX_C_SOURCE=200809L $(SRC) $(LIBS) -o build/linux/$(TARGET)
	
windows: $(SRC)
	$(WINCC) $(CFLAGS) -D_WIN32_WINNT=0x0601 $(SRC) $(WIN_LDF) -o build/windows/$(TARGET)
	
osx: $(SRC)
	xcrun $(CC) $(SRC) $(CFLAGS) $(OSX_LDF) -o build/osx/YellowFeather.app/Contents/MacOS/$(TARGET) -fno-lto
