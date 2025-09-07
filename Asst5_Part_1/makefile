#write make library to make a static library libmsocket.a

CC = gcc
CFLAGS = -Wall -g
AR = ar
ARFLAGS = rcs

all: ./initmsocket; ./user1; ./user2

user1: user1.c libmsocket
	$(CC) $(CFLAGS) -libmsocket user1 -o user1.c

user2: user2.c libmsocket
	$(CC) $(CFLAGS) -libmsocket user2 -o user2.c

initmsocket: initmsocket.c libmsocket.a
	$(CC) $(CFLAGS) -libmsocket initmsocket -o initmsocket.c

libmsocket.a: $(OBJS)
	$(AR) $(ARFLAGS) libmsocket.a msocket.o

msocket.o: msocket.c msocket.h
	$(CC) $(CFLAGS) -c msocket.c

clean:
	rm -f *.o libmsocket.a
