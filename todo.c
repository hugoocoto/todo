/* todo.c
 *
 * Desc:
 * A simple to-do list for the terminal written in C.
 * You can add tasks with dates and retrieve them later
 * by time frames like a week.
 *
 *
 * Author: Hugo Coto Florez
 * Repo: https://github.com/hugootoflorez/todo
 * License: licenseless
 * Standard: C11
 * ------------------------------------------------------*/

/* It has to be defined to use strptime and mktime
 * and greater to 499 to use strdup */
#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define FLAG_IMPLEMENTATION
#include "flag.h"

#include "da.h"

#define BACKUP_PATH "/home/hugo/"
#define TMP_FOLDER "/tmp/"
#define HIDEN "."

#define IN_FILENAME BACKUP_PATH "todo.out"
#define OUT_FILENAME BACKUP_PATH "todo.out"
#define PID_FILENAME TMP_FOLDER ".todo-daemon-pid"
#define LOG_FILENAME BACKUP_PATH HIDEN "log.txt"

#define MAX_CLIENTS 16

#define TODO(what)
#define ZERO(obj_ptr) memset((obj_ptr), 0, sizeof(obj_ptr)[0])
#define STRADD(strbuf, what, ...) sprintf((strbuf) + strlen(strbuf), what, ##__VA_ARGS__)
#define TRUNCAT(str, chr)                         \
        do {                                      \
                char *_c_;                        \
                if ((_c_ = strchr((str), (chr)))) \
                        *_c_ = 0;                 \
        } while (0)

#define ENABLE_DEBUG 0
#if defined(ENABLE_DEBUG) && ENABLE_DEBUG
#define DEBUG(format, ...) printf("[INFO] " format, ##__VA_ARGS__)
#else
#define DEBUG(...)
#endif

typedef struct {
        time_t due;
        char *name;
        char *desc;
} Task;

typedef DA(Task) Task_da;

Task_da data;
bool *quiet = NULL;

/* Chatgpt prime */
const char *no_tasks_messages[] = {
        "No tasks for this date! Enjoy your free time.",
        "You're all caught up! Maybe start something new?",
        "Nothing to do! A perfect time for a break.",
        "No tasks here! How about planning ahead?",
        "You're task-free! Go do something fun.",
        "No pending tasks! Maybe check your goals?",
        "All clear! Time to relax or explore new ideas.",
        "No deadlines today! Make the most of it.",
        "Nothing due now! Maybe organize your workspace?",
        "You're ahead of schedule! Keep up the great work."
};

#define DATETIME_FORMAT "%c"

static char *
overload_date(time_t time)
{
        static char global_datetime_buffer[64];
        strftime(global_datetime_buffer, sizeof global_datetime_buffer - 1, DATETIME_FORMAT, localtime(&time));
        return global_datetime_buffer;
}

int
compare_tasks_by_date(const void *a, const void *b)
{
        Task *ea = (Task *) a;
        Task *eb = (Task *) b;
        return (int) (ea->due - eb->due);
}

void
add_if_valid(Task task)
{
        if (task.name && task.due) {
                DEBUG("[%s]\n", task.name);
                DEBUG("  date: %s\n", overload_date(task.due));
                if (task.desc) {
                        DEBUG("  desc: %s\n", task.desc);
                }
                DEBUG("\n");

                da_append(&data, task);
        }
}

void
list_tasks(int fd, Task_da d, const char *format, ...)
{
        va_list arg;
        va_start(arg, format);
        qsort(d.data, d.size, sizeof *d.data, compare_tasks_by_date);

        if (*quiet == false) {
                vdprintf(fd, format, arg);
                dprintf(fd, ":\n");
                for_da_each(e, d)
                {
                        dprintf(fd, "%d: %s (%s)", da_index(e, d), e->name, overload_date(e->due));
                        dprintf(fd, e->desc ? ": %s\n" : "\n", e->desc);
                }
                if (d.size == 0)
                        dprintf(fd, "  %s\n", no_tasks_messages[rand() % 10]);
        }

        else if (d.size > 0) {
                vdprintf(fd, format, arg);
                dprintf(fd, ":\n");
                for_da_each(e, d)
                {
                        dprintf(fd, "%d: %s (%s)", da_index(e, d), e->name, overload_date(e->due));
                        dprintf(fd, e->desc ? ": %s\n" : "\n", e->desc);
                }
        }
}

void
serve()
{
        struct sockaddr_in sock_in;
        socklen_t addr_len;
        int sockfd;
        int clientfd;
        int port;
        char client_ip[INET_ADDRSTRLEN];
        int fd;
        pid_t pid;
        port = 5002;

        DEBUG("Serve at: ");
        printf("http://127.0.0.1:%d\n", port);

        /* Todo:
         * When launched as `firefox $(todo -serve)` it opens two clients.
         * I dont know why. It is not a critical problem but is quite anoying.
         */

        /* As fork is called twice it is not attacked to terminal */
        if (fork() != 0) {
                exit(0);
        }

        if (fork() != 0) {
                exit(0);
        }


        /* Redirect output to log file */
        fd = open(LOG_FILENAME, O_RDWR | O_APPEND | O_CREAT, 0600);
        assert(fd >= 0);
        assert(dup2(fd, STDERR_FILENO) >= 0);
        assert(dup2(fd, STDOUT_FILENO) >= 0);
        /* Close stdin to tell processes that this one is not
         * going to produce more output (otherwise $() will never exit)*/
        close(STDIN_FILENO);

        /* TODO:
         * The read pid sometimes is diferent from the last pid wrote.
         * I think that it can be reading a value before the file is
         * updated or something like that.
         * This can also be cause by a race condition but it would be
         * strange as I dont start multiple servers at the same time.
         * Also the error might be in the printf, so it would look like
         * an error but it isnt. What is sure is that some daemons where
         * never killed.
         *
         * To test it, run a daemon and see (with ps or similar) that
         * there is the only one instance of todo running. After calling
         * todo -serve more times, the previous instances should be killed.
         * If there is more than one instance alive, something where wrong.
         * Also it prints some info to LOG_FILENAME.
         */
        fd = open(PID_FILENAME, O_RDONLY | O_CREAT, 0600);
        assert(fd >= 0);
        if (read(fd, &pid, sizeof pid) == sizeof pid) {
                printf("Kill pid %d\n", pid);
                kill(pid, SIGTERM);
        }
        else {
                printf("read (kill) failed: pid %d\n", pid);
        }
        close(fd);

        fd = open(PID_FILENAME, O_WRONLY | O_TRUNC | O_CREAT, 0600);
        assert(fd >= 0);
        pid = getpid();
        assert(write(fd, &pid, sizeof pid) == sizeof pid);
        printf("write pid %d\n", pid);
        close(fd);


        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        assert(sockfd >= 0);

        sock_in.sin_family = AF_INET;
        sock_in.sin_addr.s_addr = htonl(INADDR_ANY);
        sock_in.sin_port = htons(port);
        errno = 0;
        if (bind(sockfd, (struct sockaddr *) &sock_in, sizeof(struct sockaddr_in)) < 0) {
                if (errno == EADDRINUSE) {
                        ++port;
                        fprintf(stderr, "Port %d already in use! Using %d\n", port - 1, port);
                        serve();
                }
                perror("Bind");
                exit(1);
        }

        assert(listen(sockfd, MAX_CLIENTS) >= 0);

        while (1) {
                char inbuf[128] = { 0 };
                char buf[1024 * 1024] = { 0 };
                int clicked_elem_index;
                addr_len = sizeof(struct sockaddr_in);
                DEBUG("Waiting for a new connection...\n");

                if (((clientfd = accept(sockfd, (struct sockaddr *) &sock_in, &addr_len)) < 0)) {
                        perror("Accept");
                        exit(-1);
                }

                switch (fork()) {
                case 0:
                        switch (read(clientfd, inbuf, sizeof inbuf - 1)) {
                        case 0:
                        case -1:
                                fprintf(stderr, "Internal Server Error! Reload the page\n");
                                continue;
                        default:
                                if (sscanf(inbuf, "GET /?button=%d HTTP/1.1\r\n", &clicked_elem_index) == 1) {
                                        DEBUG("Clicked button %d\n", clicked_elem_index);
                                        switch (clicked_elem_index) {
                                        default:
                                                da_remove(&data, clicked_elem_index);
                                                break;
                                        case -1:
                                                return;
                                        }
                                }
                        }

                        inet_ntop(AF_INET, &sock_in.sin_addr, client_ip, addr_len);
                        DEBUG("New connection: %s/%d\n", client_ip, ntohs(sock_in.sin_port));

                        qsort(data.data, data.size, sizeof *data.data, compare_tasks_by_date);

                        STRADD(buf, "<html>");
                        STRADD(buf, "<body>");
                        STRADD(buf, "<title>");
                        STRADD(buf, "Todo");
                        STRADD(buf, "</title>");
                        STRADD(buf, "<h1>");
                        STRADD(buf, "Tasks");
                        STRADD(buf, "</h1>");
                        STRADD(buf, "<dl>");
                        for_da_each(e, data)
                        {
                                STRADD(buf, "<dt>");
                                STRADD(buf, "%s", e->name);
                                STRADD(buf, "<form action=\"/\" method=\"GET\" style=\"display:inline;\">");
                                STRADD(buf, "<input type=\"hidden\" name=\"button\" value=\"%d\">", da_index(e, data));
                                STRADD(buf, "<button type=\"submit\">Done</button>");
                                STRADD(buf, "</form>");
                                STRADD(buf, "<dd>");
                                STRADD(buf, "%s", overload_date(e->due));
                                STRADD(buf, "</dd>");
                                if (e->desc) {
                                        STRADD(buf, "<dd><p>");
                                        STRADD(buf, "%s\n", e->desc);
                                        STRADD(buf, "</p></dd>");
                                }
                        }
                        STRADD(buf, "</dl>");
                        STRADD(buf, "<br>");
                        STRADD(buf, "<form action=\"/\" method=\"GET\" style=\"display:inline;\">");
                        STRADD(buf, "<input type=\"hidden\" name=\"button\" value=\"%d\">", -1);
                        STRADD(buf, "<button type=\"submit\">Save and quit</button>");
                        STRADD(buf, "</form>");
                        STRADD(buf, "</body>");
                        STRADD(buf, "</html>");

                        dprintf(clientfd, "HTTP/1.1 200 OK\r\n");
                        dprintf(clientfd, "Content-Type: text/html\r\n");
                        dprintf(clientfd, "Content-Length: %zu\r\n", strlen(buf));
                        dprintf(clientfd, "\r\n");

                        assert(send(clientfd, buf, strlen(buf), 0) > 0);

                        close(clientfd);
                        exit(0);
                }
        }
        close(sockfd);
}

int
load_from_file(const char *filename)
{
        FILE *f;
        char buf[128];
        Task task = { 0 };
        struct tm tp;
        char *c;

        DEBUG("LOAD from file %s\n", filename);

        f = fopen(filename, "r");
        if (f == NULL) {
                fprintf(stderr, "File %s can not be opened! You should create it\n", filename);
                return 0;
        }

        while (fgets(buf, sizeof buf - 1, f)) {
                switch (buf[0]) {
                        /* NAME */
                case '[':
                        add_if_valid(task);
                        ZERO(&task);
                        TRUNCAT(buf, ']');
                        task.name = strdup(buf + 1);
                        break;

                        /* DESCRIPTION */
                case ' ':
                        if (!memcmp(buf + 2, "desc: ", 6)) {
                                TRUNCAT(buf + 8, '\n');
                                task.desc = strdup(buf + 8);
                        }

                        /* DATE TIME */
                        else if (!memcmp(buf + 2, "date: ", 6)) {
                                TRUNCAT(buf, '\n');
                                ZERO(&tp);
                                DEBUG("Reading format %s\n", buf + 8);
                                if ((c = strptime(buf + 8, DATETIME_FORMAT, &tp)) && *c) {
                                        fprintf(stderr, "Can not load %s\n", buf + 8);
                                }

                                tp.tm_isdst = -1; // determine if summer time is in use (+-1h)
                                task.due = mktime(&tp);
                                DEBUG(
                                "Task: %s\n"
                                "\ttime = %d:%d:%d\n"
                                "\tdate = %d/%d/%d\n"
                                "\twday = %d\n"
                                "\tyday = %d\n",
                                task.name,
                                tp.tm_hour,
                                tp.tm_min,
                                tp.tm_sec,
                                tp.tm_mday,
                                tp.tm_mon,
                                tp.tm_year + 1900,
                                tp.tm_wday,
                                tp.tm_yday);
                        }

                        /* INVALID ARGUMENT */
                        else
                                fprintf(stderr, "Unknown token: %s\n", buf);
                        break;

                case '\n':
                        break;
                default:
                        fprintf(stderr, "Unknown token: %s\n", buf);
                        break;
                }
        }

        add_if_valid(task);
        fclose(f);
        return 1;
}

int
load_to_file(const char *filename)
{
        FILE *f;
        f = fopen(filename, "w");

        DEBUG("LOAD to file %s\n", filename);

        if (f == NULL) {
                fprintf(stderr, "File %s can not be opened to write!\n", filename);
                return 0;
        }

        for_da_each(task, data)
        {
                fprintf(f, "[%s]\n", task->name);
                fprintf(f, "  date: %s\n", overload_date(task->due));
                if (task->desc)
                        fprintf(f, "  desc: %s\n", task->desc);
                fprintf(f, "\n");
                DEBUG("[%s]\n", task->name);
                DEBUG("  date: %s\n", overload_date(task->due));
                if (task->desc) {
                        DEBUG("  desc: %s\n", task->desc);
                }
                DEBUG("\n");
        }

        fclose(f);
        return data.size;
}

time_t
days(unsigned int days)
{
        time_t t;
        struct tm *tp;
        t = time(NULL) + days * (3600 * 24);
        tp = localtime(&t);
        tp->tm_hour = 23;
        tp->tm_min = 59;
        tp->tm_sec = 59;
        tp->tm_isdst = -1; // determine if summer time is in use (+-1h)
        return mktime(tp);
}

time_t
next_sunday(int *d)
{
        time_t t;
        struct tm *tp;
        t = time(NULL);
        tp = localtime(&t);
        tp->tm_isdst = -1; // determine if summer time is in use (+-1h)
        mktime(tp);
        if (d)
                *d = 7 - tp->tm_wday;
        return days(7 - tp->tm_wday);
}

/* Get a subarray of DATA whose end date is before TP */
Task_da
tasks_before(struct tm tp)
{
        tp.tm_isdst = -1; // determine if summer time is in use (+-1h)
        time_t time = mktime(&tp);
        Task_da filtered_data = { 0 };

        for_da_each(task, data)
        {
                if (difftime(task->due, time) <= 0)
                        da_append(&filtered_data, *task);
        }
        return filtered_data;
}

void
destroy_all()
{
        for_da_each(e, data)
        {
                free(e->name);
                free(e->desc);
        }
        da_destroy(&data);
}

void
usage(FILE *stream)
{
        fprintf(stream, "Usage: %s [OPTIONS]\n", flag_program_name());
        fprintf(stream, "OPTIONS:\n");
        flag_print_options(stream);
}

void
add_task()
{
        Task task = { 0 };
        char buf[128];
        time_t t = time(0);
        struct tm tp_current = *localtime(&t);
        struct tm tp = { 0 };
        int n;

        /* Name */
        printf("Task name: ");
        fflush(stdout);
        if (!fgets(buf, sizeof buf - 1, stdin)[1]) {
                return;
        }
        TRUNCAT(buf, '\n');
        task.name = strdup(buf);

        /* Desc */
        printf("  Desc: ");
        fflush(stdout);
        if (fgets(buf, sizeof buf - 1, stdin)[1]) {
                TRUNCAT(buf, '\n');
                task.desc = strdup(buf);
        }

        /* Date */
        printf("  | +N: N days from today\n");
        printf("  | DD: Day DD of current month\n");
        printf("  | DD/MM: Day DD of MM month\n");
        printf("  | DD/MM/YYYY: Day DD of MM month of year YY\n");
        printf("  Date format: ");
        fflush(stdout);
        fgets(buf, sizeof buf - 1, stdin);
        if (sscanf(buf, "+%d", &n) == 1) {
                t += n * (3600 * 24);
                tp = *localtime(&t);
        } else if (sscanf(buf, "%d/%d/%d", &tp.tm_mday, &tp.tm_mon, &tp.tm_year) == 3) {
        } else if (sscanf(buf, "%d/%d", &tp.tm_mday, &tp.tm_mon) == 2) {
                tp.tm_year = tp_current.tm_year;
                --tp.tm_mon;
        } else if (sscanf(buf, "%d", &tp.tm_mday) == 1) {
                tp.tm_year = tp_current.tm_year;
                tp.tm_mon = tp_current.tm_mon;

        } else {
                fprintf(stderr, "Error: can not parse date: %s\n", buf);
                free(task.name);
                free(task.desc);
                return;
        }

        /* Time */
        printf("  | +N: N hours from now\n");
        printf("  | HH MM: hour HH and minutes MM\n");
        printf("  | Defaults to 23:59:59\n");
        printf("  Time format: ");
        fflush(stdout);
        fgets(buf, sizeof buf - 1, stdin);
        if (sscanf(buf, "+%d", &n) == 1) {
                struct tm tp2;
                t = time(NULL) + n * (3600 * 24);
                tp2 = *localtime(&t);
                tp.tm_hour = tp2.tm_hour + n;
                tp.tm_min = tp2.tm_min;
                tp.tm_sec = tp2.tm_sec;
        } else if (sscanf(buf, "%d %d", &tp.tm_hour, &tp.tm_min) == 2) {
                tp.tm_sec = 0;
        } else {
                tp.tm_hour = 23;
                tp.tm_min = 59;
                tp.tm_sec = 59;
        }

        tp.tm_isdst = -1; // determine if summer time is in use (+-1h)
        task.due = mktime(&tp);

        DEBUG(
        "Task: %s\n"
        "  time = %d:%d:%d\n"
        "  date = %d/%d/%d\n"
        "  wday = %d\n"
        "  yday = %d\n"
        "  isdst = %d\n",
        task.name,
        tp.tm_hour,
        tp.tm_min,
        tp.tm_sec,
        tp.tm_mday,
        tp.tm_mon,
        tp.tm_year + 1900,
        tp.tm_wday,
        tp.tm_yday,
        tp.tm_isdst);


        da_append(&data, task);
}

int
main(int argc, char *argv[])
{
        bool *help = flag_bool("help", false, "Print this help and exit");
        bool *today = flag_bool("today", false, "Show tasks due today");
        bool *week = flag_bool("week", false, "Show tasks due this week (tasks before Sunday)");
        int *in = flag_int("in", -1, "Show tasks due in the next N days");
        bool *overdue = flag_bool("overdue", false, "Show tasks that are past their due date");
        int *done = flag_int("done", -1, "Mark task N as completed");
        bool *clear = flag_bool("clear", false, "Mark all tasks as completed");
        bool *add = flag_bool("add", false, "Add a new task");
        char **in_file = flag_str("in_file", IN_FILENAME, "Input file");
        char **out_file = flag_str("out_file", IN_FILENAME, "Output file");
        quiet = flag_bool("quiet", false, "Show less output as usual");
        bool *host = flag_bool("serve", false, "Start http server daemon");
        bool *die = flag_bool("die", false, "Kill running daemon");

        if (!flag_parse(argc, argv)) {
                usage(stderr);
                flag_print_error(stderr);
                exit(1);
        }

        if (load_from_file(*in_file) == 0) {
                destroy_all();
                exit(0);
        }
        srand(time(0));

        if (*help) {
                usage(stdout);
                destroy_all();
                exit(0);
        }
        if (*add) {
                add_task();
        }

        if (*done >= 0) {
                qsort(data.data, data.size, sizeof *data.data, compare_tasks_by_date);
                da_remove(&data, *done);
        }

        if (*clear) {
                data.size = 0;
        }

        if (*today) {
                time_t time = days(0);
                Task_da filter = tasks_before(*localtime(&time));
                list_tasks(STDOUT_FILENO, filter, "Tasks for today");
        }

        else if (*overdue) {
                time_t t = time(NULL);
                Task_da filter = tasks_before(*localtime(&t));
                list_tasks(STDOUT_FILENO, filter, "Overdue tasks");
                da_destroy(&filter);
        }

        else if (*in >= 0) {
                time_t time = days(*in);
                Task_da filter = tasks_before(*localtime(&time));
                list_tasks(STDOUT_FILENO, filter, "Tasks for %d days", *in);
                da_destroy(&filter);
        }


        else if (*week) {
                time_t t = next_sunday(NULL);
                Task_da filter = tasks_before(*localtime(&t));
                list_tasks(STDOUT_FILENO, filter, "Tasks before Sunday");
                da_destroy(&filter);
        }

        else if (*host) {
                serve();
        }

        else if (*die) {
                int fd;
                pid_t pid;
                fd = open(PID_FILENAME, O_RDONLY | O_CREAT, 0600);
                assert(fd >= 0);
                if (read(fd, &pid, sizeof pid) == sizeof pid) {
                        DEBUG("Kill pid %d\n", pid);
                        kill(pid, SIGTERM);
                }
        }

        else {
                list_tasks(STDOUT_FILENO, data, "Tasks");
        }

        load_to_file(*out_file);
        destroy_all();
        return 0;
}
