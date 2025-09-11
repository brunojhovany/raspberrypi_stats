## Makefile to build/install Raspberry Pi stats (C++ or Python) as systemd service

PREFIX ?= /usr/local
BINDIR := $(PREFIX)/bin
UNITDIR ?= /etc/systemd/system
DEFAULTDIR ?= /etc/default

# Mode selection (cpp or python). Usage: make install MODE=python
MODE ?= cpp

# C++ build vars
TARGET := raspberrypi_stats_cpp
SRCDIR := cpp/src
BUILDDIR := build
OBJDIR := $(BUILDDIR)/obj
SOURCES := $(wildcard $(SRCDIR)/*.cpp)
OBJECTS := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))
CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wconversion -pedantic
LDFLAGS ?=

.PHONY: all clean install uninstall service-enable service-disable rebuild format install-cpp install-python uninstall-cpp uninstall-python

all: $(BUILDDIR)/$(TARGET)

$(BUILDDIR)/$(TARGET): $(OBJECTS)
	@mkdir -p $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built $@"

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILDDIR)

install: install-$(MODE)

install-cpp: all
	install -Dm755 $(BUILDDIR)/$(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -Dm644 systemd/raspberrypi_stats.service $(DESTDIR)$(UNITDIR)/raspberrypi_stats.service
	install -Dm644 systemd/raspberrypi_stats.env $(DESTDIR)$(DEFAULTDIR)/raspberrypi_stats
	@echo "[CPP] Installed binary -> $(DESTDIR)$(BINDIR)/$(TARGET)"
	@echo "[CPP] Installed service -> $(DESTDIR)$(UNITDIR)/raspberrypi_stats_cpp.service"
	@echo "Enable with: sudo systemctl daemon-reload && sudo systemctl enable --now raspberrypi_stats_cpp.service"

install-python:
	install -Dm755 raspberry_stats.py $(DESTDIR)$(BINDIR)/raspberrypi_stats_py
	install -Dm644 systemd/raspberrypi_stats.service $(DESTDIR)$(UNITDIR)/raspberrypi_stats.service
	install -Dm644 systemd/raspberrypi_stats.env $(DESTDIR)$(DEFAULTDIR)/raspberrypi_stats
	@echo "[PY] Installed script -> $(DESTDIR)$(BINDIR)/raspberrypi_stats_py"
	@echo "[PY] Installed service -> $(DESTDIR)$(UNITDIR)/raspberrypi_stats_py.service"
	@echo "Remember to have Python deps installed (psutil, Pillow, adafruit-circuitpython-ssd1306, smbus2)."
	@echo "Enable with: sudo systemctl daemon-reload && sudo systemctl enable --now raspberrypi_stats_py.service"

uninstall: uninstall-cpp uninstall-python

uninstall-cpp:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(UNITDIR)/raspberrypi_stats_cpp.service
	rm -f $(DESTDIR)$(DEFAULTDIR)/raspberrypi_stats_cpp || true
	@echo "[CPP] Removed (ignore if not installed)."

uninstall-python:
	rm -f $(DESTDIR)$(BINDIR)/raspberrypi_stats_py
	rm -f $(DESTDIR)$(UNITDIR)/raspberrypi_stats_py.service
	rm -f $(DESTDIR)$(DEFAULTDIR)/raspberrypi_stats_py || true
	@echo "[PY] Removed (ignore if not installed)."

service-enable: service-enable-$(MODE)

service-enable-cpp:
	sudo systemctl daemon-reload
	sudo systemctl enable --now raspberrypi_stats_cpp.service

service-enable-python:
	sudo systemctl daemon-reload
	sudo systemctl enable --now raspberrypi_stats_py.service

service-disable: service-disable-$(MODE)

service-disable-cpp:
	sudo systemctl disable --now raspberrypi_stats_cpp.service || true

service-disable-python:
	sudo systemctl disable --now raspberrypi_stats_py.service || true

rebuild: clean all

format:
	@echo "(Optional) Add clang-format config if desired."
