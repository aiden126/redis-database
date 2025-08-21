CXX = g++
CXXFLAGS = -Wall -Wextra -g -O0

SRCS = client.cpp server.cpp hashtable.cpp avl.cpp zset.cpp heap.cpp threadpool.cpp
OBJS = $(SRCS:.cpp=.o)

all: client server

client: client.o
	$(CXX) $(CXXFLAGS) -o $@ $^

server: server.o hashtable.o avl.o zset.o heap.o threadpool.o
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp %.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o client server