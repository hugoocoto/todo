OUT = todo
FLAGS = -Wall -Wextra -std=c11 -ggdb

todo: todo.o
	gcc $(FLAGS) todo.o -o $(OUT)

todo.o: todo.c flag.h da.h
	gcc -c todo.c $(FLAGS)

install: todo
	cp todo ~/.local/bin/
