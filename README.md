# todo

## Name
Todo - A simple to-do list for the terminal written in C.
You can add tasks with dates and retrieve them later
by time frames like a week.

## Synopsis
todo [OPTIONS]

## Description
run todo -help. If you need more documentation, read the source code.

## Version Status
Currently it is stable enought to be used.

## Installation
#### Installation from source
Please take a look to the default vaules in options.h before build it.
```sh
git clone https://github.com/hugocotoflorez/todo
cd todo
make
```
The makefile compliles the sources and copy the executable to
`~/.local/bin/`. Make sure it is in the PATH to be able to use
`todo` without full route.

#### Issues:
`cp: cannot create regular file '/home/hugo/.local/bin/todo': Text file busy
make: *** [makefile:6: install] Error 1`: Just kill daemon and run make again:
`todo -die` and then `make` again.

## Http task visualizer

Running `todo -serve` creates a daemon that serve a http client
in the address returned by the command.

![Example Image](./serve1.png)

You can deploy it automatically using `xdg-open $(todo -serve)` or
using the desired browser.

#### CSS
CSS can be modified without restarting the server.
Tools like darkviwer alter colors.
