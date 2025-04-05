#define FROG_IMPLEMENTATION
#include "thirdparty/frog/frog.h"

#define CC "gcc"
#define FLAGS "-Wall", "-Wextra"
#define OUT "todo"

int
main(int argc, char *argv[])
{
        frog_rebuild_itself(argc, argv);
        frog_cmd_wait(CC, FLAGS, "todo.c", "-o", OUT, NULL);
        frog_shell_cmd("cp ./todo ~/.local/bin/todo");

        return 0;
}

