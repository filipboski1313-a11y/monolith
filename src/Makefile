CXX ?= g++
PREFIX ?= /usr/local
DESTDIR ?=
BIN_DIR ?= ../bin
TARGET = $(BIN_DIR)/monolith-settings
SRC = main.cpp

CXXFLAGS = -O3 -march=native -pipe -std=c++20 -flto -Wall -Wextra -Wno-unused-parameter $(shell pkg-config --cflags libadwaita-1 gtk4 cairo)
LDFLAGS = -flto -s $(shell pkg-config --libs libadwaita-1 gtk4 cairo)

all: $(TARGET)

$(TARGET): $(SRC) settings.hpp bezier_widget.hpp grain_generator.hpp version.hpp
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/monolith-settings

.PHONY: all clean install
