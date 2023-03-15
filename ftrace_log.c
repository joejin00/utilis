/*
 * ftrace_log -- Pull trace_pipe and save to disk
 *
 * Compile: gcc -g -o ftrace_log ftrace_log.c -Wall
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <sys/file.h>
#include <linux/limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define DEBG    0
#define INFO    1
#define WARN    2
#define ERR     3

#define dprintf(lvl, fmt, args...) do {                         \
        if ((lvl) >= debug_level)                               \
                fprintf(stdout, (fmt) , ##args );               \
} while (0)

#define MAX_LOGS 10

const char *prog = "ftrace_log";
const char *pidfile = "/run/ftrace_log.pid";
int pidfile_fd;

size_t max_filesz = 100 << 20;          /* Log file size, Default: 100M */
unsigned long nr_logs = MAX_LOGS;       /* Number of logfile */
char log_path[PATH_MAX] = "/var/log/ftrace";/* Path of log file */
char log_file[PATH_MAX];                /* Log file with full path */
char compress_cmd[PATH_MAX] = "/bin/gzip"; /* Command for compress */
char *ftrace_pipe = "/sys/kernel/debug/tracing/trace_pipe";     /* Ftrace pipe file */
FILE *ftrace_fp;                        /* Read fp, trace_pipe */
FILE *log_fp;                           /* Write fp */
int timestamp = 0;                      /* Add timetamp to log file or no */
char *suffix = "gz";                    /* Suffix for compression */
int compress = 1;                       /* Flag of compress, default: Enabled */
int debug_level = WARN;                 /* Debug level */
int forground = 0;


#define VERSION "2023.03.07"

void usage(char *err_msg)
{
        if (err_msg)
                fprintf(stderr, "[ERROR]: %s\n", err_msg);

        fprintf(stderr, "Usage: %s [OPTION]...\n", prog);
        fprintf(stderr, "Version: %s\n\n", VERSION);        
        fprintf(stderr, "    -c            : Compress the log. Default: enabled\n");
        fprintf(stderr, "    -d dbg_lvl    : Set debug log level. Default: 2. {0: Debug, 1: Info, 2: Warn, 3: error}!\n");
        fprintf(stderr, "    -f            : Start it on forground\n");
        fprintf(stderr, "    -h|H          : Print this message!\n");
        fprintf(stderr, "    -n nr_log     : Max number of log files to be saved. Default: 10, max: 10.\n");
        fprintf(stderr, "    -p log_path   : Path to save log file. Default: /var/log/ftrace\n");
        fprintf(stderr, "    -s log_filesz : Log file size, Default: 100M, max 4096M.\n");
        fprintf(stderr, "    -t            : Add wallclock to the log. default: disabled\n");
        fprintf(stderr, "                  : [WARN]: The timestamp may not matched with log produce time\n");
        fprintf(stderr, "\n\n");

        if (err_msg)
                exit(-1);
        exit(0);
}

int validate_and_set_path(char *path, char *dest, int mode)
{
        /* Path is too long */
        if (strlen(path) > PATH_MAX - 1)
                return -1;

        /* Validate file permission */
        if (access(path, mode) != 0)
                return -1;

        dprintf(DEBG, "File %s permission %d is ok!\n", path, mode);

        /* Validate the path permission only */
        if (dest == NULL)
                return 0;

        /* Copy the path to destention */
        strncpy(dest, path, PATH_MAX);

        return 0;
}

int set_max_filesz(char *s_sz)
{
        if (!s_sz || !strlen(s_sz))
                return -1;

        switch(s_sz[strlen(s_sz) - 1]) {
                case 'k':
                case 'K':
                        max_filesz = atol(s_sz) << 10;
                        break;
                case 'm':
                case 'M':
                        max_filesz = atol(s_sz) << 20;
                        break;
                /* Bytes */
                case 'b':
                case 'B':
                case '0' ... '9':
                        max_filesz = atol(s_sz);
                        break;
                default:
                        return -1;
        }
        dprintf(DEBG, "max_filesz: %ld\n", max_filesz);
        return ((max_filesz <= 0 || (max_filesz >> 30) > 4) ? -1 : 0);
}

int open_logfile(void)
{
        log_fp = fopen(log_file, "a+");
        if (log_fp == NULL) {
                dprintf(ERR, "Can not open logfile %s\n", log_file);
                exit(-1);
        }
        return 0;
}

void do_rotate_and_compress(void)
{
        int i;
        char cur_file[PATH_MAX], new_file[PATH_MAX];
        char cmd[PATH_MAX];

        /* Close log file */
        if (log_fp) {
                fclose(log_fp);
                log_fp = NULL;
        }

        /* In rotating, remove the oldest logfile */
        snprintf(cur_file, PATH_MAX, "%s.%lu.%s", log_file, nr_logs - 1, suffix);
        dprintf(DEBG, "Remove %s\n", cur_file);
        remove(cur_file);

        for (i = nr_logs - 2; i >= 0; i--) {
                snprintf(cur_file, PATH_MAX, "%s.%d.%s", log_file, i, suffix);
                if (access(cur_file, R_OK|W_OK) != 0)
                        continue;
                snprintf(new_file, PATH_MAX, "%s.%d.%s", log_file, i + 1, suffix);
                dprintf(DEBG, "Rename %s => %s\n", cur_file, new_file);
                if (rename(cur_file, new_file)) {
                        dprintf(ERR, "Failed to rename file %s\n", cur_file);
                        exit(-1);
                }
        }

        /* Rename current logfile to .0 */
        snprintf(new_file, PATH_MAX, "%s.0", log_file);
        dprintf(DEBG, "Rename %s => %s\n", log_file, new_file);
        if (rename(log_file, new_file)) {
                dprintf(ERR, "Failed to rename file %s\n", log_file);
                exit(-1);
        }

        /* Compress the logfile */
        snprintf(cmd, PATH_MAX, "%s %s", compress_cmd, new_file);
        if (system(cmd) != 0) {
                dprintf(ERR, "Faile to execut %s\n", cmd);
                exit(-1);
        }
        
        /* Create new log file */
        if (open_logfile() != 0) {
                dprintf(ERR, "Failed to open %s(%s)\n", log_file, strerror(errno));
                exit(-1);
        }

        return;
}

void sig_handler (int signum)
{
        dprintf(DEBG, "Got signal %d\n", signum);
        if (log_fp) {
                fflush(log_fp);
                fclose(log_fp);
                log_fp = NULL;
        }

        if (ftrace_fp) {
                fclose(ftrace_fp);
                ftrace_fp = NULL;
        }

        unlink(pidfile);

        exit(-1);
}

void set_sigs(void)
{
        struct sigaction action;

        action.sa_handler = sig_handler;
        sigemptyset (&action.sa_mask);
        action.sa_flags = 0;

        sigaction (SIGINT, &action, NULL);
        sigaction (SIGHUP, &action, NULL);
        sigaction (SIGTERM, &action, NULL);
        sigaction (SIGSTOP, &action, NULL);
        sigaction (SIGKILL, &action, NULL);
        sigaction (SIGQUIT, &action, NULL);
}

int main(int argc, char **argv)
{
        char opt;
        char *line = NULL;
        size_t len = 0;
        ssize_t nread;
        size_t total_write = 0;
        time_t curtime;
        char time_str[80];


        /* Make sure ftrace has mounted to /sys/kernel/debug/tracing */
        if (validate_and_set_path(ftrace_pipe, NULL, R_OK|W_OK) != 0)
                usage("Can not find /sys/kernel/debug/tracing/trace_pipe");

        while ((opt = getopt(argc, argv, "s:n:p:chHtd:f")) != -1) {
                switch (opt) {
                        case 's':
                                if (set_max_filesz(optarg) != 0)
                                        usage("Invalid log file size");
                                break;
                        case 'n':
                                nr_logs = atoi(optarg);
                                if (nr_logs <= 0 || nr_logs > MAX_LOGS)
                                        usage("Invalid NR_logs");
                                break;
                        case 'p':
                                if (validate_and_set_path(optarg, log_path, R_OK|W_OK) != 0)
                                        usage("Invalid log path!");
                                break;
                        case 'c':
                                compress = 1;
                                break;
                        case 't':
                                timestamp = 1;
                                break;
                        case 'd':
                                debug_level = atoi(optarg);
                                if (debug_level == DEBG)
                                        forground = 1;
                                break;
                        case 'f':
                                forground = 1;
                                break;
                        case 'h':
                        default:
                                usage(NULL);
                }
        }

        /* construct log_file with full path */
        snprintf(log_file, PATH_MAX, "%s/%s.log", log_path, prog);
        dprintf(DEBG, "***** Setting *****\n");
        dprintf(DEBG, "filesz: %ld\n", max_filesz);
        dprintf(DEBG, "nr_logs: %ld\n", nr_logs);
        dprintf(DEBG, "log_path: %s\n", log_path);
        dprintf(DEBG, "compress_cmd: %s\n", compress_cmd);

        if (open_logfile() != 0) {
                dprintf(ERR, "Failed to open %s(%s)\n", log_file, strerror(errno));
                exit(-1);
        }

        /* Get current logfile size */
        fseek(log_fp, 0, SEEK_END);
        total_write = ftell(log_fp);
        dprintf(DEBG, "total_write: %lu\n", total_write);

        ftrace_fp = fopen(ftrace_pipe, "r");
        if (ftrace_fp == NULL) {
                dprintf(ERR, "Failed to open %s(%s)\n", ftrace_pipe, strerror(errno));
                exit(-1);
        }

        set_sigs();

        /* Run as daemon? */
        if (forground == 0) {
                if (daemon(0, 0)) {
                        dprintf(ERR, "Failed to daemon(%s)\n", strerror(errno));
                        exit(-1);
                }

                pidfile_fd = open(pidfile, O_WRONLY|O_CREAT|O_EXCL|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
                if (pidfile_fd >= 0) {
                        char str[16];
                        snprintf(str, sizeof(str), "%u\n", getpid());
                        write(pidfile_fd, str, strlen(str));
                        close(pidfile_fd);
                } else {
                        dprintf(ERR, "Failed to write pidfile %s\n", pidfile);
                        exit(-1);
                }
        }



        while ((nread = getline(&line, &len, ftrace_fp)) != -1) {
                /* Write timestamp */
                if (timestamp) {
                        curtime = time(NULL);
                        strncpy(time_str, ctime(&curtime), 80);
                        time_str[strlen(time_str) - 1] = '\0';
                        total_write += fprintf(log_fp, "%s:", time_str);
                }
                total_write += fprintf(log_fp, "%s", line);

                /* Do log rotate and compress */
                if (total_write > max_filesz) {
                        dprintf(DEBG, "total_write: %lu\n", total_write);
                        /* Rotate */
                        do_rotate_and_compress();
                        total_write = 0;
                }
        }
        fclose(ftrace_fp);
        fclose(log_fp);
        unlink(pidfile);

        return 0;
}
