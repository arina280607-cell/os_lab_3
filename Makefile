.RECIPEPREFIX := >
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2 -g -pthread

lab3: main.o filter.o
>$(CC) $(CFLAGS) main.o filter.o -o lab3

main.o: main.c filter.h
>$(CC) $(CFLAGS) -c main.c -o main.o

filter.o: filter.c filter.h
>$(CC) $(CFLAGS) -c filter.c -o filter.o

.PHONY: clean
clean:
>rm -f main.o filter.o lab3
