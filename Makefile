CC      ?= cc
CFLAGS  += -std=c11 -Wall -Wextra -O2
CPPFLAGS+= \
    $(shell pkg-config --cflags libudev x11 xrandr libkmod ddcutil)

LDFLAGS += \
    $(shell pkg-config --libs libudev x11 xrandr libkmod ddcutil)

TARGET  = ddcci-hotplugd
SRC     = src/ddcci-hotplugd.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
