CC      ?= cc
CFLAGS  += -D_FORTIFY_SOURCE=3 -std=c11 -Wall -Wextra -O2
CPPFLAGS+= \
    $(shell pkg-config --cflags libudev x11 xrandr libkmod ddcutil xau)

LDFLAGS += \
    $(shell pkg-config --libs libudev x11 xrandr libkmod ddcutil xau)

TARGET  = ddcci-hotplugd
SRC     = src/*.c

PREFIX        ?= /usr
LIBEXECDIR    ?= $(PREFIX)/lib
SYSTEMDUNITDIR?= $(PREFIX)/lib/systemd/system

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^ $(LDFLAGS)

install: $(TARGET)
	install -d $(DESTDIR)$(LIBEXECDIR)
	install -m 0755 $(TARGET) \
		$(DESTDIR)$(LIBEXECDIR)/$(TARGET)

	install -d $(DESTDIR)/etc
	install -m 0644 systemd/$(TARGET).env \
		$(DESTDIR)/etc/$(TARGET).env

	install -d $(DESTDIR)$(SYSTEMDUNITDIR)
	install -m 0644 systemd/$(TARGET).service \
		$(DESTDIR)$(SYSTEMDUNITDIR)/$(TARGET).service

uninstall:
	rm -f \
		$(DESTDIR)$(LIBEXECDIR)/$(TARGET) \
		$(DESTDIR)$(SYSTEMDUNITDIR)/$(TARGET).service
clean:
	rm -f $(TARGET)

.PHONY: all clean
