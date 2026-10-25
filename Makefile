# Claymore@Linux
#
# The CLI and the driver have no dependencies beyond a C++17 compiler and
# libc. The GUI needs GTK+ 3 development headers; if they are missing the GUI
# target is skipped and everything else still builds.

# VERSION is the one place the version lives: 'make package' names the .deb
# after it and .github/workflows/release.yml refuses a tag that disagrees with
# it. An explicit VERSION=... still wins, for a one-off local build.
VERSION ?= $(shell cat VERSION 2>/dev/null || echo 1.0.0)

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow -Iinclude
LDFLAGS  ?=

BUILD    := build
BIN      := bin

# ------------------------------------------------------------------ install --
#
# DESTDIR is the staging root that packagers set and that 'make package' uses;
# it is empty for a real install. Everything else is a normal PREFIX layout,
# so a user who cannot write to /usr/local can do:
#
#     make PREFIX=$HOME/.local install
#
PREFIX      ?= /usr/local
DESTDIR     ?=
BINDIR      ?= $(PREFIX)/bin
DATADIR     ?= $(PREFIX)/share
MANDIR      ?= $(DATADIR)/man
APPSDIR     ?= $(DATADIR)/applications
ICONDIR     ?= $(DATADIR)/icons/hicolor
UDEVDIR     ?= $(PREFIX)/lib/udev/rules.d
USERUNITDIR ?= $(PREFIX)/lib/systemd/user

# The desktop file is named after the application id, because that is what the
# window tells the desktop about itself (_GTK_APPLICATION_ID, which the window
# manager uses to find the launcher and therefore the icon in the task bar).
# StartupWMClass inside it covers the desktops that match on WM_CLASS instead.
DESKTOP_FILE := org.claymore.linux.desktop

# The icon the desktop entry, the launcher and the panel look up.
ICON_NAME   := claymore-linux
ICON_SIZES  := 16 24 32 48 64 128 256

# ------------------------------------------------------------------ package --
DEB_ARCH ?= $(shell dpkg --print-architecture 2>/dev/null || uname -m)
DEB_NAME := claymore-linux_$(VERSION)_$(DEB_ARCH)
DEB_ROOT := $(BUILD)/deb/$(DEB_NAME)
DEB_FILE := $(BUILD)/$(DEB_NAME).deb

CORE_SRC := \
	src/color.cpp \
	src/design.cpp \
	src/settings.cpp \
	src/layout.cpp \
	src/layout_data.cpp \
	src/hidraw.cpp \
	src/protocol.cpp \
	src/effects.cpp \
	src/device.cpp \
	src/sync_server.cpp

CLI_SRC  := src/main.cpp
TEST_SRC := tests/test_core.cpp

CORE_OBJ := $(CORE_SRC:%.cpp=$(BUILD)/%.o)
CLI_OBJ  := $(CLI_SRC:%.cpp=$(BUILD)/%.o)
TEST_OBJ := $(TEST_SRC:%.cpp=$(BUILD)/%.o)

GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0 2>/dev/null)
GTK_LIBS   := $(shell pkg-config --libs gtk+-3.0 2>/dev/null)

GUI_AVAILABLE := $(shell pkg-config --exists gtk+-3.0 2>/dev/null && echo yes)
GUI_SRC := \
	gui/main_gui.cpp \
	gui/app.cpp \
	gui/icon.cpp \
	gui/panels.cpp \
	gui/keyboard_view.cpp \
	gui/tray.cpp

GUI_OBJ := $(GUI_SRC:%.cpp=$(BUILD)/%.o)

.PHONY: all cli gui test check clean run help install uninstall package \
        verify-docs verify-gui verify-design verify-install

all: cli $(if $(GUI_AVAILABLE),gui,) test

cli: $(BIN)/claymore

$(BIN)/claymore: $(CORE_OBJ) $(CLI_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

gui: $(BIN)/claymore-gui

$(BIN)/claymore-gui: $(CORE_OBJ) $(GUI_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $(GTK_CFLAGS) -o $@ $^ $(GTK_LIBS) $(LDFLAGS)

# GUI sources need the GTK include path; the driver and CLI do not.
$(BUILD)/gui/%.o: gui/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(GTK_CFLAGS) -c -o $@ $<

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/tests/run_tests: $(CORE_OBJ) $(TEST_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test: $(BUILD)/tests/run_tests
	@$(BUILD)/tests/run_tests

# Keep the agent-facing map and its Makefile references honest.
verify-docs:
	@tools/verify_code_map.py

# The keyboard canvas cannot be unit tested, so photograph the running window
# and check that it really painted keys. Needs an X server; see the script.
verify-gui: $(BIN)/claymore-gui
	@tools/verify_gui_render.sh

# Painting a key is the design tab's whole purpose and the one thing the unit
# tests cannot reach: this drives real clicks and checks the pixels.
verify-design: $(BIN)/claymore-gui
	@tools/verify_design_tab.sh

# An install target that is never exercised is an install target that is
# broken, so this stages one and looks at what came out.
verify-install: $(BIN)/claymore
	@tools/verify_install.sh

# ------------------------------------------------------------------ install --
#
# The CLI is installed first and deliberately: everything below it is data, and
# the icon files are the GUI drawing itself out, so there is one drawing for
# the window, the panel, the task bar and the launcher.
install: cli $(if $(GUI_AVAILABLE),gui,)
	@echo "installing $(VERSION) into $(DESTDIR)$(PREFIX)"
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(BIN)/claymore $(DESTDIR)$(BINDIR)/claymore

	@if [ -n "$(GUI_AVAILABLE)" ]; then \
	    install -m 755 $(BIN)/claymore-gui $(DESTDIR)$(BINDIR)/claymore-gui; \
	    install -d $(BUILD)/icons; \
	    $(BIN)/claymore-gui --export-icons $(BUILD)/icons >/dev/null; \
	    for size in $(ICON_SIZES); do \
	        install -d $(DESTDIR)$(ICONDIR)/$${size}x$${size}/apps; \
	        install -m 644 $(BUILD)/icons/$(ICON_NAME)-$$size.png \
	            $(DESTDIR)$(ICONDIR)/$${size}x$${size}/apps/$(ICON_NAME).png; \
	    done; \
	else \
	    echo "note: GTK+ 3 was not available, so the GUI and its icons were skipped"; \
	fi

	install -d $(DESTDIR)$(ICONDIR)/scalable/apps
	install -m 644 assets/$(ICON_NAME).svg $(DESTDIR)$(ICONDIR)/scalable/apps/$(ICON_NAME).svg

	install -d $(DESTDIR)$(APPSDIR)
	install -m 644 contrib/$(DESKTOP_FILE) $(DESTDIR)$(APPSDIR)/$(DESKTOP_FILE)

	install -d $(DESTDIR)$(UDEVDIR)
	install -m 644 udev/99-claymore-linux.rules $(DESTDIR)$(UDEVDIR)/99-claymore-linux.rules

	install -d $(DESTDIR)$(USERUNITDIR)
	install -m 644 contrib/claymore-design.service $(DESTDIR)$(USERUNITDIR)/claymore-design.service

	install -d $(DESTDIR)$(MANDIR)/man1
	gzip -9 -n -c docs/claymore.1 > $(DESTDIR)$(MANDIR)/man1/claymore.1.gz
	chmod 644 $(DESTDIR)$(MANDIR)/man1/claymore.1.gz
	gzip -9 -n -c docs/claymore-gui.1 > $(DESTDIR)$(MANDIR)/man1/claymore-gui.1.gz
	chmod 644 $(DESTDIR)$(MANDIR)/man1/claymore-gui.1.gz

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/claymore $(DESTDIR)$(BINDIR)/claymore-gui
	rm -f $(DESTDIR)$(APPSDIR)/$(DESKTOP_FILE)
	rm -f $(DESTDIR)$(ICONDIR)/scalable/apps/$(ICON_NAME).svg
	rm -f $(DESTDIR)$(UDEVDIR)/99-claymore-linux.rules
	rm -f $(DESTDIR)$(USERUNITDIR)/claymore-design.service
	rm -f $(DESTDIR)$(MANDIR)/man1/claymore.1.gz $(DESTDIR)$(MANDIR)/man1/claymore-gui.1.gz
	@for size in $(ICON_SIZES); do \
	    rm -f $(DESTDIR)$(ICONDIR)/$${size}x$${size}/apps/$(ICON_NAME).png; \
	done
	@echo "removed $(VERSION) from $(DESTDIR)$(PREFIX)"
	@echo "your designs in ~/.config/claymore are untouched"

# --------------------------------------------- a .deb built out of the tree --
#
# Deliberately built from what 'make install' stages rather than from a
# separate list of files: a package that installs something other than what
# the install target installs is the classic way for the two to disagree.
package: $(DEB_FILE)

$(DEB_FILE): cli $(if $(GUI_AVAILABLE),gui,)
	@tools/build_deb.sh "$(VERSION)" "$(DEB_ARCH)" "$(abspath $(DEB_ROOT))" \
	                    "$(abspath $(DEB_FILE))"

check: test

clean:
	rm -rf $(BUILD) $(BIN)

run:
	./bin/claymore-gui

help:
	@echo "make            - build the CLI, the GUI (if GTK+ 3 is present) and the tests"
	@echo "make cli        - build bin/claymore only"
	@echo "make gui        - build bin/claymore-gui"
	@echo "make test       - build and run the hardware-free test suite"
	@echo "make install    - install into PREFIX (/usr/local); DESTDIR stages it"
	@echo "make uninstall  - remove an installation, keeping your designs"
	@echo "make package    - build a .deb with dpkg-deb, no debhelper needed"
	@echo "make verify-docs     - validate the agent code map and Makefile references"
	@echo "make verify-gui     - launch the GUI, photograph it and check it painted"
	@echo "make verify-design  - drive the design tab and check that clicks paint"
	@echo "make verify-install - stage an install and check every file in it"
	@echo "make clean      - remove build output"
	@echo
	@echo "  PREFIX=\$$HOME/.local make install   installs without root"
	@echo "  VERSION=1.2.3 make package          names the .deb"
