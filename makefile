OUT = todo
FLAGS = -Wall -Wextra -std=c11

todo: flag.h da.h todo.o
	gcc $(FLAGS) todo.o -ggdb -o $(OUT)

todo.o: todo.c
	gcc -c todo.c

