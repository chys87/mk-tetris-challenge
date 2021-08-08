CXX := clang++
CXXFLAGS := -O3 -g -std=gnu++20 -march=native -Wall -Wextra -pipe -flto -fno-exceptions -fomit-frame-pointer -fno-stack-protector -pthread
LIBS := -labsl_strings -labsl_raw_hash_set -labsl_hash -lgflags -ljemalloc

.PHONY: all

all: main


main: $(wildcard *.cc) $(wildcard *.h)
	$(CXX) $(CXXFLAGS) -o $@ $(wildcard *.cc) $(LIBS)
