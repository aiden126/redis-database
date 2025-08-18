CXX = g++
CXXFLAGS = -Wall -Wextra

SRCS = client.cpp server.cpp hashtable.cpp avl.cpp zset.cpp
OBJS = $(SRCS.cpp=.o)

all: client server

client: client.o
	$(CXX) $(CXXFLAGS) -o $@ $^

server: server.o hashtable.o avl.o zset.o
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp %.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o client server