CXX = g++
CXXFLAGS = -std=c++17 -pthread -O3 -Wall
TARGET = hss
SRC = hss.cpp

all: compile run

compile:
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

run:
	@echo "Running HSS with seed=42, threads=4, epsilon=0.1, size=1M"
	./$(TARGET) 42 4 0.1 1000000

run-verbose:
	@echo "Running with verbose output"
	./$(TARGET) 42 4 0.1 1000000 --verbose

clean:
	rm -f $(TARGET) *.o

.PHONY: all compile run run-verbose clean