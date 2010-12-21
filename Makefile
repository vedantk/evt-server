CC = gcc
OPTFLAGS = -Wextra -Os
CFLAGS = -Wall $(OPTFLAGS)

evt-server.o: evt-server.c

clean:
	rm -f evt-server.o
