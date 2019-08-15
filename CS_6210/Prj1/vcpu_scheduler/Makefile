CC=gcc
CFLAGS=-std=c99 -Wall -I. -pthread -lvirt -lm

all: cpu_scheduler

cpu_scheduler: vcpu_scheduler.c
	$(CC) -o vcpu_scheduler vcpu_scheduler.c $(CFLAGS)

clean:
	rm vcpu_scheduler
