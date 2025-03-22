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
#include <time.h>

#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FLAG_IMPLEMENTATION
#include "flag.h"

#define TODO(what)

#include "da.h"

#define BACKUP_PATH ""
#define IN_FILENAME BACKUP_PATH "todo.out"
#define OUT_FILENAME BACKUP_PATH "todo.out"

#define ZERO(obj_ptr) memset((obj_ptr), 0, sizeof(obj_ptr)[0])

#define TRUNCAT(str, chr)                         \
        do {                                      \
                char *_c_;                        \
                if ((_c_ = strchr((str), (chr)))) \
                        *_c_ = 0;                 \
        } while (0)


#define DEBUG 0
#if defined(DEBUG) && DEBUG
#define LOG(format, ...) printf("INFO: " format, ##__VA_ARGS__)
#else
#define LOG(...)
#endif

typedef struct {
        time_t due;
        char *name;
        char *desc;
} Task;

typedef DA(Task) Task_da;

Task_da data;

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

void
add_if_valid(Task task)
{
        if (task.name && task.due) {
                LOG("[%s]\n", task.name);
                LOG("  date: %s\n", overload_date(task.due));
                if (task.desc)
                        LOG("  desc: %s\n", task.desc);
                LOG("\n");

                da_append(&data, task);
        }
}

int
load_from_file(const char *filename)
{
        FILE *f;
        char buf[128];
        Task task = { 0 };

        LOG("LOAD from file %s\n", filename);

        f = fopen(filename, "r");
        if (f == NULL) {
                fprintf(stderr, "File %s can not be read!\n", filename);
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
                        /* DATE */
                        else if (!memcmp(buf + 2, "date: ", 6)) {
                                struct tm tp = { 0 };
                                if (!strptime(buf + 8, DATETIME_FORMAT, &tp)) {
                                        fprintf(stderr, "Can not load %s\n", buf + 8);
                                }

                                task.due = mktime(&tp);
                        }
                        /* INVALID ARGUMENT */
                        else
                                fprintf(stderr, "Unknown token: %s\n", buf);
                        break;

                case '\n':
                        break;
                default:
                        fprintf(stderr, "Unknown token: %s\n", buf);
                        continue;
                }
        }

        add_if_valid(task);
        fclose(f);
        return data.size;
}

int
load_to_file(const char *filename)
{
        FILE *f;
        f = fopen(filename, "w");

        LOG("LOAD to file %s\n", filename);

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
        return mktime(tp);
}

time_t
next_sunday()
{
        time_t t;
        struct tm *tp;
        t = time(NULL);
        tp = localtime(&t);
        return days(7 - tp->tm_wday);
}

/* Get a subarray of DATA whose end date is before TP */
Task_da
tasks_before(struct tm tp)
{
        time_t time = mktime(&tp);
        Task_da filtered_data;
        da_init(&filtered_data);

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

int
compare_tasks_by_date(const void *a, const void *b)
{
        Task *ea = (Task *) a;
        Task *eb = (Task *) b;
        return (int) (ea->due - eb->due);
}

void
list_tasks(Task_da d, const char *format, ...)
{
        va_list arg;
        va_start(arg, format);
        qsort(d.data, d.size, sizeof *d.data, compare_tasks_by_date);
        vprintf(format, arg);
        printf(":\n");
        for_da_each(e, d)
        {
                printf("%d: %s (%s)", da_index(e, d), e->name, overload_date(e->due));
                printf(e->desc ? ": %s\n" : "\n", e->desc);
        }
        if (d.size == 0)
                printf("  %s\n", no_tasks_messages[rand() % 10]);
}

void
add_task()
{
        Task task = { 0 };
        char buf[128];
        struct tm tp = { 0 };
        time_t t;

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
        int n;
        if (sscanf(buf, "+%d", &n) == 1) {
                t = time(NULL) + n * (3600 * 24);
                tp = *localtime(&t);
        } else if (sscanf(buf, "%d/%d/%d", &tp.tm_mday, &tp.tm_mon, &tp.tm_year) == 3) {
        } else if (sscanf(buf, "%d/%d", &tp.tm_mday, &tp.tm_mon) == 2) {
        } else if (sscanf(buf, "%d", &tp.tm_mday) == 1) {
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
                tp.tm_min = tp2.tm_min + n;
                tp.tm_sec = tp2.tm_sec + n;
        } else if (sscanf(buf, "%d %d", &tp.tm_hour, &tp.tm_min) == 2) {
                tp.tm_sec = 0;
        } else {
                tp.tm_hour = 23;
                tp.tm_min = 59;
                tp.tm_sec = 59;
        }

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
        bool *add = flag_bool("add", false, "Add a new task");
        char **in_file = flag_str("in_file", IN_FILENAME, "Input file");
        char **out_file = flag_str("out_file", IN_FILENAME, "Output file");

        if (!flag_parse(argc, argv)) {
                usage(stderr);
                flag_print_error(stderr);
                exit(1);
        }

        da_init(&data);
        load_from_file(*in_file);
        srand(time(0));

        if (*help) {
                usage(stdout);
                destroy_all();
                exit(0);
        }

        if (*today) {
                time_t time = days(0);
                Task_da filter = tasks_before(*localtime(&time));
                printf("Tasks for today:\n");
                for_da_each(e, filter)
                {
                        printf("  %s (%s)", e->name, overload_date(e->due));
                        printf(e->desc ? ": %s\n" : "\n", e->desc);
                }
                if (filter.size == 0)
                        printf("  %s\n", no_tasks_messages[rand() % 10]);
                da_destroy(&filter);
        }

        else if (*overdue) {
                time_t t = time(NULL);
                Task_da filter = tasks_before(*localtime(&t));

                printf("Overdue tasks:\n");
                for_da_each(e, filter)
                {
                        printf("  %s (%s)", e->name, overload_date(e->due));
                        printf(e->desc ? ": %s\n" : "\n", e->desc);
                }
                if (filter.size == 0)
                        printf("  %s\n", no_tasks_messages[rand() % 10]);
                da_destroy(&filter);
        }

        else if (*in >= 0) {
                time_t time = days(*in);
                Task_da filter = tasks_before(*localtime(&time));
                list_tasks(filter, "Tasks for %d days", *in);
                da_destroy(&filter);
        }


        else if (*week) {
                time_t time = next_sunday();
                Task_da filter = tasks_before(*localtime(&time));
                list_tasks(filter, "Tasks for %d days", *in);
                da_destroy(&filter);
        }

        else {
                list_tasks(data, "Tasks");
        }

        if (*add) {
                add_task();
        }

        if (*done >= 0) {
                qsort(data.data, data.size, sizeof *data.data, compare_tasks_by_date);
                da_remove(&data, *done);
        }


        load_to_file(*out_file);
        destroy_all();
        return 0;
}
