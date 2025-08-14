CXX = g++
CXXFLAGS = -Wall -Wextra

all: client server

client: client.cpp
	$(CXX) $(CXXFLAGS) -o client client.cpp

server: server.cpp hashtable.cpp hashtable.h
	$(CXX) $(CXXFLAGS) -o server server.cpp hashtable.cpp

clean:
	rm -f client server