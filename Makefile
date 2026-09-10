PREFIX ?= $(HOME)/.local
PKG = gio-2.0 json-glib-1.0
CFLAGS ?= -O2 -g -fPIC -Wall -Wextra -Wno-unused-parameter
CFLAGS += -Isrc $(shell pkg-config --cflags $(PKG))
LIBS = $(shell pkg-config --libs $(PKG)) -ldl

SRC = src/rewrite.c src/catalog.c src/config.c src/dedupe.c
PRELOAD_SRC = $(SRC) src/preload.c

.PHONY: all test install uninstall clean

all: build/libvalent-notify.so build/test-rewrite

build/libvalent-notify.so: $(PRELOAD_SRC) src/rewrite.h src/config.h src/dedupe.h
	mkdir -p build
	$(CC) $(CFLAGS) -shared -o $@ $(PRELOAD_SRC) $(LIBS)

build/test-rewrite: tests/test_rewrite.c $(SRC) src/rewrite.h src/config.h src/dedupe.h
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ tests/test_rewrite.c $(SRC) $(LIBS)

test: build/test-rewrite
	./build/test-rewrite

install: all test
	mkdir -p $(PREFIX)/lib $(PREFIX)/bin
	install -m755 build/libvalent-notify.so $(PREFIX)/lib/libvalent-notify.so
	install -m755 contrib/valent-notify $(PREFIX)/bin/valent-notify
	@mkdir -p $(HOME)/.config/valent-notify
	@if [ ! -f $(HOME)/.config/valent-notify/config.json ]; then \
		install -m644 config.example.json $(HOME)/.config/valent-notify/config.json; \
		echo "wrote $(HOME)/.config/valent-notify/config.json"; \
	fi
	@python3 contrib/patch-valent-wrapper.py
	@python3 contrib/patch-dbus-service.py
	@systemctl --user enable ca.andyholmes.Valent.service >/dev/null 2>&1 || true
	@python3 contrib/patch-swaync.py || true
	@echo "installed. restart Valent to load the preload (valent-notify restart)"

uninstall:
	rm -f $(PREFIX)/lib/libvalent-notify.so $(PREFIX)/bin/valent-notify

clean:
	rm -rf build
