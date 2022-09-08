CC = g++
CFLAGS = -Wall -g -luring

first: all

all: cdn

cdn: main.o
	$(CC) $(CFLAGS) -o cdn main.o Engine.o Http.o Cache.o Md5.o Server.o Dns.o Utils.o

main.o: Cache.o Http.o Engine.o Md5.o Server.o Dns.o Utils.o
	$(CC) $(CFLAGS) -c ./main.cpp

test: main-test.o
	$(CC) $(CFLAGS) -o test main-test.o Http.o Cache.o Md5.o Dns.o

main-test.o: Http.o Cache.o Md5.o Dns.o
	$(CC) $(CFLAGS) -c ./main-test.cpp

Engine.o: ./src/Engine.hpp ./src/Cache.hpp ./src/Server.hpp ./src/Dns.hpp
	$(CC) $(CFLAGS) -c ./src/Engine.cpp

Http.o: ./src/Http.hpp
	$(CC) $(CFLAGS) -c ./src/Http.cpp

Cache.o: ./src/Cache.hpp ./src/Md5.hpp
	$(CC) $(CFLAGS) -c ./src/Cache.cpp

Md5.o: ./src/Md5.hpp
	$(CC) $(CFLAGS) -c ./src/Md5.cpp

Server.o: ./src/Server.hpp
	$(CC) $(CFLAGS) -c ./src/Server.cpp

Dns.o: ./src/Dns.hpp
	$(CC) $(CFLAGS) -c ./src/Dns.cpp

Utils.o: ./src/Utils.hpp
	$(CC) $(CFLAGS) -c ./src/Utils.cpp

clean:
	rm -f cdn test
	rm -f *.o
