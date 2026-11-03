CXX ?= g++
PREFIX ?= /usr/local
DESTDIR ?=
BIN_DIR ?= ../bin
TARGET = $(BIN_DIR)/monolith-toolbox
SRC = main.cpp

CXXFLAGS = -O3 -march=native -pipe -std=c++20 -flto -Wall -Wextra -Wno-unused-parameter $(shell pkg-config --cflags gtk4 gtk4-layer-shell-0 cairo)
LDFLAGS = -flto -s $(shell pkg-config --libs gtk4 gtk4-layer-shell-0 cairo)

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/monolith-toolbox

.PHONY: all clean install
