CC=gcc
CFLAGS= -pthread -lrt -Wall

all: main

main:
	$(CC) -o tpc main.c $(CFLAGS)