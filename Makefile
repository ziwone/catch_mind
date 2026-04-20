TARGET = catch_mind
PING_TARGET = cm_net_ping
PING_HOST_TARGET = cm_net_ping_host
CC = aarch64-linux-gnu-g++
HOST_CC = g++
CC_C = aarch64-linux-gnu-gcc
CFLAGS = -Wall -std=c++17
INCLUDE = -Iinclude
SOURCES = src/main.cpp src/game.cpp src/display.cpp src/bgm.cpp
PING_SOURCES = src/net_ping.cpp
FB_SERVER_OBJ = src/fb_server.o

all: $(TARGET) $(PING_TARGET) $(PING_HOST_TARGET)
	sudo cp $(TARGET) /nfsroot
	sudo cp $(PING_TARGET) /nfsroot

$(FB_SERVER_OBJ): src/fb_server.c
	$(CC_C) -O2 -Wall -c -o $@ src/fb_server.c

$(TARGET): $(SOURCES) $(FB_SERVER_OBJ)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $(SOURCES) $(FB_SERVER_OBJ) -lpthread

$(PING_TARGET): $(PING_SOURCES)
	$(CC) $(CFLAGS) -o $@ $(PING_SOURCES)

$(PING_HOST_TARGET): $(PING_SOURCES)
	$(HOST_CC) $(CFLAGS) -o $@ $(PING_SOURCES)

clean:
	rm -f $(TARGET) $(PING_TARGET) $(PING_HOST_TARGET) $(FB_SERVER_OBJ)

.PHONY: all clean
