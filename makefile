
CC = clang

FLAGS = -std=c11 -Wall -Wextra -O3 -flto -Isrc -Isrc/vendor/lua \
	-lm -lGL -ldl -lpthread -lX11 -lXi -lXcursor -lasound \
	-DLUA_USE_POSIX -D_POSIX_C_SOURCE=200809L \

WINFLAGS = --target=x86_64-w64-windows-gnu -fuse-ld=lld \
	 -std=c11 -Wall -Wextra -O3 -flto -Isrc -Isrc/vendor/lua \
	 -lmingw32 -mwindows -lkernel32 -ld3d11 -lole32 \
	 -D_WIN32_WINNT=0x0601 \

OSXFLAGS = -x objective-c \
	 -std=c11 -Wall -Wextra -O3 -Isrc -Isrc/vendor/lua  \
   	 -framework OpenGL \
	 -framework Cocoa \
	 -framework QuartzCore \
	 -framework AudioToolbox \
	 -framework IOKit \
	 -framework CoreFoundation \
	 -framework Foundation \
	 -lSystem \
   	 -O2 -Wall \
   	 -Wl,-rpath,@executable_path/../Frameworks \
   	 -Wl,-rpath,/var/home/dytu/.osxcross/lib \
   	 -arch x86_64 -arch arm64 \
   	 -mmacosx-version-min=11.0 -fobjc-link-runtime \
	
TARGET = yf

LUA_SRC = $(filter-out src/vendor/lua/lua.c src/vendor/lua/luac.c, \
              $(wildcard src/vendor/lua/*.c))
              
SRC = $(LUA_SRC) $(wildcard src/hardware/*.c) \
			src/config.c src/main.c

all: $(TARGET)


$(TARGET): $(SRC)
	$(CC) $(SRC) -o build/linux/$(TARGET) $(FLAGS)
	
windows: $(SRC)
	$(CC) $(SRC) -o build/windows/$(TARGET) $(WINFLAGS)
	
osx: $(SRC)
	xcrun $(CC) $(OSXFLAGS) $(SRC) -o build/osx/YellowFeather.app/Contents/MacOS/$(TARGET)
