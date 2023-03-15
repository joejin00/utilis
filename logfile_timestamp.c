/*
 * logfile_timestamp -- pull the log file and add timestamp
 *
 * Command for compile: gcc -Wall -o logfile_timestamp logfile_timestamp.c
 *
 * Usage: ./logfile_timestamp input_logfile output_logfile
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>

static long get_filesz(int fd)
{
        struct stat stat;

        memset(&stat, 0, sizeof(stat));
        if (fstat(fd, &stat) < 0)
                return -1;

        return stat.st_size;
}

int main(int argc, char **argv)
{
        int fd = 0, out = 0;
        char buf[1024];
        long sz = 0, newsz = 0;
        struct timespec tm = {0, 1};
        ssize_t ret;

        if (argc != 3) {
                fprintf(stderr, "Usage %s input output\n", argv[0]);
                return -1;
        }

        fd = open(argv[1], O_RDONLY|O_NONBLOCK);
        if (fd < 0) {
                perror("open: ");
                fprintf(stderr, "Usage %s filename\n", argv[0]);
                return -1;
        }

        out = open(argv[2], O_RDWR|O_APPEND|O_CREAT, 0660);
        if (out < 0) {
                perror("open: ");
                return -1;
        }

        sz = get_filesz(fd);
        while(1) {
                memset(buf, 0, sizeof(buf));
                ret = read(fd, buf, sizeof(buf));

                if (ret > 0) {
                        int i;
                        for (i = 0; i < strlen(buf); i++) {
                                if (buf[i] != '\n') {
                                        write(out, &buf[i], 1);
                                        continue;
                                } else {
                                        time_t now = time(NULL);
                                        char *time_str = ctime(&now);
                                        time_str[strlen(time_str)-1] = 0;
                                        write(out, "\n[", 1);
                                        write(out, time_str, strlen(time_str));
                                        write(out, "]: ", 3);
                                }
                        }
                } else {
                        nanosleep(&tm, NULL);
                }

                newsz = get_filesz(fd);
                if (!newsz && sz) {
                        time_t now = time(NULL);
                        char *time_str = ctime(&now);
                        time_str[strlen(time_str)-1] = 0;

                        write(out, "[", 1);
                        write(out, time_str, strlen(time_str));
                        write(out, "] ********** \n", 14);
                        lseek(fd, 0, SEEK_SET);
                }
                sz = newsz;
        }

        close(fd);
        close(out);
        return 0;
}
