/* It has to be defined to use strptime
 * and greater to 499 to use strdup */
#define _XOPEN_SOURCE 500
#include <time.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#include "da.h"

#define BACKUP_PATH ""
#define in_filename "todo.txt"
#define out_filename "todo.txt"

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
        time_t end;
        char *name;
        char *desc;
} Entry;

typedef DA(Entry) entry_da;

entry_da data;

#define DATETIME_FORMAT "%c"
static char *
overload_date(time_t time)
{
        static char global_datetime_buffer[64];
        strftime(global_datetime_buffer, sizeof global_datetime_buffer - 1, DATETIME_FORMAT, localtime(&time));
        return global_datetime_buffer;
}

void
add_if_valid(Entry entry)
{
        if (entry.name && entry.end) {
                // LOG("ADD\n");
                // LOG("[%s] @%p\n", entry.name, entry.name);
                // LOG("  date: %s\n", overload_date(entry.end));
                // if (entry.desc)
                //         LOG("  desc: %s\n", entry.desc);

                da_append(&data, entry);
        }
}


int
load_from_file(const char *filename)
{
        FILE *f;
        char buf[128];
        Entry entry;
        struct tm tp;

        ZERO(&entry);

        f = fopen(filename, "r");
        if (f == NULL) {
                fprintf(stderr, "File %s can not be read!\n", filename);
                return 0;
        }

        while (fgets(buf, sizeof buf - 1, f)) {
                switch (buf[0]) {
                        /* NAME */
                case '[':
                        add_if_valid(entry);
                        ZERO(&entry);
                        TRUNCAT(buf, ']');
                        entry.name = strdup(buf + 1);
                        LOG("Name parsed: %s\n", buf + 1);
                        break;
                        /* DESCRIPTION */
                case ' ':
                        if (!memcmp(buf + 2, "desc: ", 6)) {
                                entry.desc = strdup(buf + 8);
                                LOG("Desc parsed: %s\n", buf + 8);
                        }
                        /* DATE */
                        else if (!memcmp(buf + 2, "date: ", 6)) {
                                *strptime(buf + 8, DATETIME_FORMAT, &tp) = 0;
                                LOG("Data parsed: %s\n", buf + 8);
                                entry.end = mktime(&tp);
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

        add_if_valid(entry);
        fclose(f);
        return data.size;
}

/* Load DATA to file named FILENAME */
int
load_to_file(const char *filename)
{
        FILE *f;
        f = fopen(filename, "w");

        if (f == NULL) {
                fprintf(stderr, "File %s can not be opened to write!\n", filename);
                return 0;
        }

        for_da_each(entry, data)
        {
                LOG("[%s]\n", entry->name);
                LOG("  date: %s\n", overload_date(entry->end));
                if (entry->desc)
                        LOG("  desc: %s\n", entry->desc);

                fprintf(f, "[%s]\n", entry->name);
                fprintf(f, "  date: %s\n", overload_date(entry->end));
                if (entry->desc)
                        fprintf(f, "  desc: %s\n", entry->desc);
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

/* Get a subarray of DATA whose end date is before TP */
entry_da
until(struct tm tp)
{
        time_t time = mktime(&tp);
        entry_da filtered_data;
        da_init(&filtered_data);

        for_da_each(entry, data)
        {
                if (difftime(entry->end, time) <= 0)
                        da_append(&filtered_data, *entry);
        }
        return filtered_data;
}

int
main(int argc, char *argv[])
{
        Entry date;

        da_init(&data);
        load_from_file(BACKUP_PATH in_filename);

        // date.name = "Task for 1 day";
        // date.end = days(1);
        // da_append(&data, date);
        //
        // date.name = "Task for 2 day";
        // date.end = days(2);
        // da_append(&data, date);

        time_t time = days(4);
        entry_da filter = until(*localtime(&time));

        printf("Filtered data:\n");
        for_da_each(e, filter)
        {
                printf("  [%s] for %s\n", e->name, overload_date(e->end));
        }
        da_destroy(&filter);

        load_to_file(BACKUP_PATH out_filename);

        for_da_each(e, data)
        {
                free(e->name);
                free(e->desc);
        }
        da_destroy(&data);
}
