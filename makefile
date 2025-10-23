OUT = todo
FLAGS = -Wall -Wextra -std=c11 -ggdb
CC = gcc

todo: todo.o
	gcc $(FLAGS) todo.o -o $(OUT)

install: todo ~/.local/bin/todo

uninstall:
	rm ~/.local/bin/todo

~/.local/bin/todo:
	mkdir -p ~/.local/bin
	cp todo ~/.local/bin/

todo.o: todo.c flag.h frog.h options.h
	gcc -c todo.c $(FLAGS)

clean: uninstall
	rm -f todo
