TARGET = catch_mind
CC = aarch64-linux-gnu-g++
CC_C = aarch64-linux-gnu-gcc
CFLAGS = -Wall -std=c++17
INCLUDE = -Iinclude
SOURCES = src/main.cpp src/game.cpp src/display.cpp src/bgm.cpp
FB_SERVER_OBJ = src/fb_server.o

all: $(TARGET)
	sudo cp $(TARGET) /nfsroot

$(FB_SERVER_OBJ): src/fb_server.c
	$(CC_C) -O2 -Wall -c -o $@ src/fb_server.c

$(TARGET): $(SOURCES) $(FB_SERVER_OBJ)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $(SOURCES) $(FB_SERVER_OBJ) -lpthread

clean:
	rm -f $(TARGET) $(FB_SERVER_OBJ)

.PHONY: all clean
