CC=gcc
CFLAGS=-std=c99 -Wall -I. -pthread -lvirt

all: memory_coordinator

memory_coordinator: memory_coordinator.c
	$(CC) -o memory_coordinator memory_coordinator.c $(CFLAGS)

clean:
	rm memory_coordinator
