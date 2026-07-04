
CC = clang

FLAGS = -std=c11 -Wall -Wextra -O3 -flto -Isrc -Ivendor/lua \
	-lSDL2 -lm -lGL -ldl -lm -lpthread -lX11 -lXi -lXcursor -lasound \
	-DLUA_USE_POSIX -D_POSIX_C_SOURCE=200809L \

WINFLAGS =  -std=c11 -Wall -Wextra -O3 -flto -Isrc -Ivendor/lua \
	 -lmingw32 -mwindows -lkernel32 \
	 -D_WIN32_WINNT=0x0601 \

OSXFLAGS =  -std=c11 -Wall -Wextra -O3 -flto -Isrc -Ivendor/lua \
   	 -framework Metal \
	 -framework Cocoa \
	 -framework QuartzCore \
	 -framework AudioToolbox \
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
	$(CC) $(FLAGS) $(SRC) -o build/linux/$(TARGET)
	
windows: $(SRC)
	$(CC) --target=x86_64-w64-windows-gnu $(WINFLAGS) $(SRC) -o build/windows/$(TARGET)
	
osx: $(SRC)
	xcrun $(CC) $(OSXFLAGS) $(SRC) -o build/osx/YellowFeather.app/Contents/MacOS/$(TARGET) -fno-lto
