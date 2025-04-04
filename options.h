#define HIDEN "."

#define BACKUP_PATH "/home/hugo/"
#define TMP_PATH "/tmp/"
#define CSS_PATH "code/todo/"

#define IN_FILENAME BACKUP_PATH "todo.out"
#define OUT_FILENAME BACKUP_PATH "todo.out"
#define CSS_FILENAME BACKUP_PATH CSS_PATH "styles.css"
#define LOG_FILENAME BACKUP_PATH HIDEN "log.txt"
#define PID_FILENAME TMP_PATH "todo-daemon-pid"

#define PORT 5002
#define MAX_ATTEMPTS 10
#define MAX_CLIENTS 16
#define BUFSIZE 1024 * 1024 /* IO buffer */

#define WRITE_TO_LOG 1

/* Please note that modifying this macro would break all previously
 * loaded tasks. As tasks are not modificable, changing this variable
 * invalidates all yet created tasks. It can be modified if needed. */
#define DATETIME_FORMAT "%c"
#define DATETIME_MAXLEN 64
