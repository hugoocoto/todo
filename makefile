OUT = todo
FLAGS = -Wall -Wextra -std=c11 -ggdb

todo: flag.h da.h todo.o
	gcc $(FLAGS) todo.o -o $(OUT)

todo.o: todo.c
	gcc -c todo.c $(FLAGS)

