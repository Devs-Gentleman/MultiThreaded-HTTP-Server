CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -g
LDFLAGS ?=
SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:.cpp=.o)
BIN := http_server

.PHONY: build run clean test all

all: build

build: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $(BIN) $(LDFLAGS)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: build
	./$(BIN)

clean:
	rm -f $(OBJ) $(BIN)

test:
	@echo "No tests yet."
