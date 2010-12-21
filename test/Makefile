CC = gcc
LIBS = -levent -lpthread
OPTFLAGS = -Wextra -g
CFLAGS = -Wall $(OPTFLAGS) $(LIBS) -I../
LINK = ../evt-server.o

OBJS = evt_test core_test flooder
all: $(OBJS)

evt_test: evt_test.c $(LINK)
core_test: core_test.c $(LINK)
flooder: flooder.c $(LINK)

clean:
	rm -f $(OBJS)

