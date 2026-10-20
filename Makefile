CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -g -fsanitize=address,undefined

.PHONY: all clean

all: client server

client: client.cpp net.cpp socket.hpp net.hpp
	$(CXX) $(CXXFLAGS) client.cpp net.cpp -o client

server: server.cpp socket.hpp net.hpp
	$(CXX) $(CXXFLAGS) net.cpp server.cpp -o server

clean:
	rm -f client server
