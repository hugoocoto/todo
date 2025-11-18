/* todo.c
 *
 * Desc:
 * A simple to-do list for the terminal written in C.
 * You can add tasks with dates and retrieve them later
 * by time frames like a week. With http server!
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
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FLAG_IMPLEMENTATION
#include "flag.h"

#include "frog.h"

#include "options.h"

#define TRUNCAT(str, chr)                         \
        do {                                      \
                char *_c_;                        \
                if ((_c_ = strchr((str), (chr)))) \
                        *_c_ = 0;                 \
        } while (0)


typedef struct {
        time_t due;
        char *name;
        char *desc;
} Task;

typedef DA(Task) Task_da;

Task_da data;
char **out_file;
char **css_file;
bool *quiet = NULL;

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

/* I can override this and use it to print so I can avoid printing the year if
 * its the same as the actual and avoid printing the time if its the default */
static char *
overload_date(time_t time)
{
        static char global_datetime_buffer[DATETIME_MAXLEN];
        strftime(global_datetime_buffer, sizeof global_datetime_buffer - 1, DATETIME_FORMAT, localtime(&time));
        return global_datetime_buffer;
}

static int
compare_tasks_by_date(const void *a, const void *b)
{
        Task *ea = (Task *) a;
        Task *eb = (Task *) b;
        return (int) (ea->due - eb->due);
}

static inline void
add_if_valid(Task task)
{
        if (task.name && task.due) {
                da_append(&data, task);
        }
}

static void
list_tasks(int fd, Task_da d, const char *format, ...)
{
        va_list arg;
        va_start(arg, format);
        qsort(d.data, d.size, sizeof *d.data, compare_tasks_by_date);

        if (!*quiet) {
                vdprintf(fd, format, arg);
                dprintf(fd, ":\n");
        }
        for_da_each(e, d)
        {
                dprintf(fd, "%d: %s (%s)", da_index(e, d), e->name, overload_date(e->due));
                dprintf(fd, e->desc ? ": %s\n" : "\n", e->desc);
        }
        if (d.size == 0 && !*quiet)
                dprintf(fd, "  %s\n", no_tasks_messages[rand() % 10]);
}

static int
load_from_file(const char *filename)
{
        FILE *f;
        char buf[128];
        Task task = { 0 };
        struct tm tp = { 0 };
        char *c;

        f = fopen(filename, "r");
        if (f == NULL) {
                fclose(fopen(filename, "w")); // create file
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
                                if ((c = strptime(buf + 8, DATETIME_FORMAT, &tp)) && *c) {
                                        LOG("Can not load %s\n", buf + 8);
                                }

                                tp.tm_isdst = -1; // determine if summer time is in use (+-1h)
                                task.due = mktime(&tp);
                        }

                        /* INVALID ARGUMENT */
                        else
                                LOG("Unknown token: %s\n", buf);
                        break;

                case '\n':
                        break;
                default:
                        LOG("Unknown token: %s\n", buf);
                        break;
                }
        }

        add_if_valid(task);
        fclose(f);
        return 0;
}

static int
load_to_file(const char *filename)
{
        FILE *f;
        f = fopen(filename, "w");

        if (f == NULL) {
                LOG("File %s can not be opened to write!\n", filename);
                return 0;
        }

        for_da_each(task, data)
        {
                fprintf(f, "[%s]\n", task->name);
                fprintf(f, "  date: %s\n", overload_date(task->due));
                if (task->desc)
                        fprintf(f, "  desc: %s\n", task->desc);
                fprintf(f, "\n");
        }

        fclose(f);
        return data.size;
}

static void
kill_self()
{
        static sem_t *sem = NULL;
        pid_t pid;
        int fd;

        if (!sem) {
                sem = sem_open("/todo_pid_file_sem", O_CREAT, 0600, 1);
        }
        assert(sem != SEM_FAILED);

        sem_wait(sem);

        fd = open(PID_FILENAME, O_RDONLY | O_CREAT, 0600);
        assert(fd >= 0);
        while (read(fd, &pid, sizeof pid) == sizeof pid) {
                kill(pid, SIGTERM);
        }
        close(fd);

        fd = open(PID_FILENAME, O_WRONLY | O_TRUNC | O_CREAT, 0600);
        assert(fd >= 0);
        pid = getpid();
        assert(write(fd, &pid, sizeof pid) == sizeof pid);
        close(fd);

        sem_post(sem);
}

/* Shared info between serve loop and serve_gen_response thread */
struct serve_data {
        struct sockaddr_in sock_in;
        int clientfd;
        int addr_len;
};

static void *
serve_gen_response(void *args)
{
        struct serve_data sdata = *(struct serve_data *) args;
        char buf[BUFSIZE];
        char css_file_buf[1024];
        int clicked_elem_index;
        int fd;
        int n;

        if (sdata.clientfd < 0) {
                LOG("invalid clientfd\n");
                return NULL;
        }

        switch (read(sdata.clientfd, buf, sizeof buf - 1)) {
        default:
                if (sscanf(buf, "GET /?button=%d HTTP/1.1", &clicked_elem_index) == 1) {
                        switch (clicked_elem_index) {
                        default:
                                /* Buttons from 0 to tasks num - 1 */
                                da_remove(&data, clicked_elem_index);
                                break;
                        case -1:
                                /* Save button */
                                load_to_file(*out_file);
                                break;
                        }
                        break;
                }
                if (strncmp(buf, "GET /favicon.ico HTTP/1.1", 25) == 0) {
                        /* The client ask for the icon. As it is not needed,
                         * return and dont send anything to the client. */
                        return NULL;
                }
                break;

        case 0:
        case -1:
                LOG("Internal Server Error! Reload the page\n");
                return NULL;
        }

        qsort(data.data, data.size, sizeof *data.data, compare_tasks_by_date);

        *buf = 0; // set buf size to 0

        /* ---------- INLINE HTML ---------- */

        strcatf(buf, "<!DOCTYPE html>");
        strcatf(buf, "<html>");
        strcatf(buf, "<head>");

        /* Try to open and load CSS file directly into <style> ... </style>. */
        fd = open(*css_file, O_RDONLY);
        if (fd >= 0) {
                strcatf(buf, "<style>");
                while ((n = read(fd, css_file_buf, sizeof css_file_buf - 1)) > 0) {
                        css_file_buf[n] = 0;
                        strcatf(buf, "%s", css_file_buf);
                }
                close(fd);
                strcatf(buf, "</style>");
        } else
                LOG("Error: cant load css file '%s'\n", *css_file);

        strcatf(buf, "</head>");
        strcatf(buf, "<body>");
        strcatf(buf, "<title>");
        strcatf(buf, "Todo");
        strcatf(buf, "</title>");
        strcatf(buf, "<h1>");
        strcatf(buf, "Tasks");
        strcatf(buf, "</h1>");
        strcatf(buf, "<dl>");

        for_da_each(e, data)
        {
                strcatf(buf, "<dt>");
                strcatf(buf, "%s", e->name);
                strcatf(buf, "<form action=\"/\" method=\"GET\" style=\"display:inline;\">");
                strcatf(buf, "<input type=\"hidden\" name=\"button\" value=\"%d\">", da_index(e, data));
                strcatf(buf, "<button type=\"submit\">Done</button>");
                strcatf(buf, "</form>");
                strcatf(buf, "<dd>");
                strcatf(buf, "%s", overload_date(e->due));
                strcatf(buf, "</dd>");
                if (e->desc) {
                        strcatf(buf, "<dd><p>");
                        strcatf(buf, "%s\n", e->desc);
                        strcatf(buf, "</p></dd>");
                }
        }

        strcatf(buf, "</dl>");
        strcatf(buf, "<br>");
        strcatf(buf, "<form action=\"/\" method=\"GET\" style=\"display:inline;\">");
        strcatf(buf, "<input type=\"hidden\" name=\"button\" value=\"%d\">", -1);
        strcatf(buf, "<button type=\"submit\">Save</button>");
        strcatf(buf, "</form>");
        strcatf(buf, "</body>");
        strcatf(buf, "</html>");

        dprintf(sdata.clientfd, "HTTP/1.1 200 OK\r\n");
        dprintf(sdata.clientfd, "Content-Type: text/html\r\n");
        dprintf(sdata.clientfd, "Content-Length: %zu\r\n", strlen(buf));
        dprintf(sdata.clientfd, "\r\n");

        assert(send(sdata.clientfd, buf, strlen(buf), 0) > 0);

        close(sdata.clientfd);
        return 0;
}

static void
spawn_serve()
{
        static int port = PORT;
        struct sockaddr_in sock_in;
        pthread_t thread_id;
        socklen_t addr_len;
        int sockfd;
        int clientfd;
        int status;

        /* As fork is called twice it is not attacked to terminal */
        if (fork() != 0) {
                exit(0);
        }

        if (fork() != 0) {
                exit(0);
        }

        kill_self();

        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        assert(sockfd >= 0);

        sock_in.sin_family = AF_INET;
        sock_in.sin_addr.s_addr = htonl(INADDR_ANY);

retry:
        errno = 0;
        sock_in.sin_port = htons(port);

        if (bind(sockfd, (struct sockaddr *) &sock_in, sizeof(struct sockaddr_in)) < 0) {
                if (errno == EADDRINUSE) {
                        ++port;
                        if (port - PORT > MAX_ATTEMPTS) {
                                perror("Bind max attempts");
                                exit(1);
                        }
                        goto retry;
                }
                perror("Bind");
                exit(1);
        }

        assert(listen(sockfd, MAX_CLIENTS) >= 0);

        /* Show the address before close descriptors so it can be redirected
         * Example: ~$ firefox $(todo -serve)
         * It should open a client in the browser */
        printf("http://127.0.0.1:%d\n", port);

        /* Man page of close said that it flushes descriptors, but I need to
         * do this to be able to redirect output to browser without errors */
        fflush(stdout);
        fflush(stderr);

        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        /* Open logfile at stdin and stdout */
        assert(open(LOG_FILENAME, O_CREAT | O_WRONLY | O_APPEND, 0600) >= 0);
        assert(open(LOG_FILENAME, O_CREAT | O_WRONLY | O_APPEND, 0600) >= 0);

        close(STDIN_FILENO);

        while (1) {
                addr_len = sizeof(struct sockaddr_in);

                if (((clientfd = accept(sockfd, (struct sockaddr *) &sock_in, &addr_len)) < 0)) {
                        LOG("accept: %s\n", strerror(errno));
                        break;
                }

                struct serve_data sdata = (struct serve_data) {
                        .sock_in = sock_in,
                        .clientfd = clientfd,
                        .addr_len = addr_len,
                };

                if ((status = pthread_create(&thread_id, NULL, serve_gen_response, &sdata)) != 0) {
                        LOG("pthread_create: %s\n", strerror(status));
                        break;
                } else
                        pthread_detach(thread_id);
        }
        /* Really it never reaches this */
        close(sockfd);
        UNREACHABLE("out of daemon loop");
}

static time_t
days(unsigned int days)
{
        time_t t;
        struct tm *tp = { 0 };
        t = time(NULL) + days * (3600 * 24);
        tp = localtime(&t);
        tp->tm_hour = 23;
        tp->tm_min = 59;
        tp->tm_sec = 59;
        tp->tm_isdst = -1; // determine if summer time is in use (+-1h)
        return mktime(tp);
}

static time_t
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
static Task_da
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

static void
destroy_all()
{
        for_da_each(e, data)
        {
                free(e->name);
                free(e->desc);
        }
        da_destroy(&data);
}

static void
usage(FILE *stream)
{
        fprintf(stream, "Usage: %s [OPTIONS]\n", flag_program_name());
        fprintf(stream, "OPTIONS:\n");
        flag_print_options(stream);
}

static void
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
                tp.tm_year -= 1900;
                --tp.tm_mon;

        } else if (sscanf(buf, "%d/%d", &tp.tm_mday, &tp.tm_mon) == 2) {
                tp.tm_year = tp_current.tm_year;
                --tp.tm_mon;

        } else if (sscanf(buf, "%d", &tp.tm_mday) == 1) {
                tp.tm_year = tp_current.tm_year;
                tp.tm_mon = tp_current.tm_mon;

        } else {
                LOG("Error: can not parse date: %s\n", buf);
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
        out_file = flag_str("out_file", IN_FILENAME, "Output file");
        css_file = flag_str("css_file", CSS_FILENAME, "CSS file");
        bool *serve = flag_bool("serve", false, "Start http server daemon");
        bool *die = flag_bool("die", false, "Kill running daemon");
        quiet = flag_bool("quiet", false, "Do not show unneded output");

        srand(time(0));

        if (!flag_parse(argc, argv)) {
                usage(stderr);
                flag_print_error(stderr);
                exit(1);
        }

        load_from_file(*in_file);

        /* The if(...) without else show tasks list.
         * The if(...) with else do not show default list tasks */

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

        if (*overdue) {
                time_t t = time(NULL);
                Task_da filter = tasks_before(*localtime(&t));
                list_tasks(STDOUT_FILENO, filter, "Overdue tasks");
                da_destroy(&filter);
        }

        else if (*today) {
                time_t time = days(0);
                Task_da filter = tasks_before(*localtime(&time));
                list_tasks(STDOUT_FILENO, filter, "Tasks for today");
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

        else if (*serve) {
                spawn_serve();
        }

        else if (*die) {
                kill_self();
                sem_unlink("/todo_pid_file_sem");
        }

        else {
                list_tasks(STDOUT_FILENO, data, "Tasks");
        }

        load_to_file(*out_file);
        destroy_all();
        return 0;
}
