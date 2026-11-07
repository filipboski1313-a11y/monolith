CXX ?= g++
PREFIX ?= /usr/local
DESTDIR ?=
BIN_DIR ?= ../bin
TARGET = $(BIN_DIR)/monolith-constellation
LEGACY_TARGET = $(BIN_DIR)/monolith-taskgraph
SRC = main.cpp

CXXFLAGS = -O3 -march=native -pipe -std=c++20 -flto -Wall -Wextra -Wno-unused-parameter -I../src $(shell pkg-config --cflags gtk4 gtk4-layer-shell-0 cairo)
LDFLAGS = -flto -s $(shell pkg-config --libs gtk4 gtk4-layer-shell-0 cairo)

all: $(TARGET)

$(TARGET): $(SRC) ../src/grain_generator.hpp ../src/hypr_ipc.hpp
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)
	cp -f $(TARGET) $(LEGACY_TARGET)
	@echo "Build complete: $(TARGET)"

clean:
	rm -f $(TARGET) $(LEGACY_TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/monolith-constellation
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/monolith-taskgraph

.PHONY: all clean install
