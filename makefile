OUT = todo
FLAGS = -Wall -Wextra -std=c11 -ggdb
CC = gcc

install: todo
	cp todo ~/.local/bin/

todo: todo.o
	gcc $(FLAGS) todo.o -o $(OUT)

todo.o: todo.c flag.h frog.h options.h
	gcc -c todo.c $(FLAGS)
