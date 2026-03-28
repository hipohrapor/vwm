CC      = gcc
CFLAGS  = -std=c99 -O2 -Wall -Wextra -pedantic
LDFLAGS = -lX11
TARGET  = vwm

all: $(TARGET)

$(TARGET): vwm.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)/usr/local/bin/$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all install clean
