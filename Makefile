CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -g -fsanitize=address,undefined

.PHONY: all clean

all: client server

client: client.cpp
	$(CXX) $(CXXFLAGS) client.cpp -o client

server: server.cpp
	$(CXX) $(CXXFLAGS) server.cpp -o server

clean:
	rm -f client server
