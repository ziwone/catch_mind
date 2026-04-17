TARGET = catch_mind
PING_TARGET = cm_net_ping
PING_HOST_TARGET = cm_net_ping_host
CC = aarch64-linux-gnu-g++
HOST_CC = g++
CFLAGS = -Wall -std=c++17
INCLUDE = -Iinclude
SOURCES = src/main.cpp src/game.cpp src/display.cpp src/bgm.cpp
PING_SOURCES = src/net_ping.cpp

all: $(TARGET) $(PING_TARGET) $(PING_HOST_TARGET)
	sudo cp $(TARGET) /nfsroot
	sudo cp $(PING_TARGET) /nfsroot

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $(SOURCES) -lpthread

$(PING_TARGET): $(PING_SOURCES)
	$(CC) $(CFLAGS) -o $@ $(PING_SOURCES)

$(PING_HOST_TARGET): $(PING_SOURCES)
	$(HOST_CC) $(CFLAGS) -o $@ $(PING_SOURCES)

clean:
	rm -f $(TARGET) $(PING_TARGET) $(PING_HOST_TARGET)

.PHONY: all clean
