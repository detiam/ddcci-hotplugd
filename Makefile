CC      ?= cc
CFLAGS  += -std=c11 -Wall -Wextra -O2
CPPFLAGS+= \
    $(shell pkg-config --cflags libudev x11 xrandr libkmod ddcutil xau)

LDFLAGS += \
    $(shell pkg-config --libs libudev x11 xrandr libkmod ddcutil xau)

TARGET  = ddcci-hotplugd
SRC     = src/*.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
