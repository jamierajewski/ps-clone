#Jamie Rajewski 3020090 CMPT360 Assignment 2

CC = gcc
CFLAGS = -Wall -g -lpthread

.PHONY: clean all

all: ps_server

ps_server: ps_server.c sort.c sort.h
	$(CC) $(CFLAGS) -o ps_server ps_server.c sort.c sort.h

clean:
	rm ps_server
