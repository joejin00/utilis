/*
 * dentry-stat -- Report Linux kernel dentry information.
 *
 * (C) 2020 by Joe Jin (joejin <at> oracle.com)
 *
 ***************************************************************************
 * This program is free software; you can redistribute it and/or modify it *
 * under the terms of the GNU General Public License as published  by  the *
 * Free Software Foundation; either version 2 of the License, or (at  your *
 * option) any later version.                                              *
 *                                                                         *
 * This program is distributed in the hope that it  will  be  useful,  but *
 * WITHOUT ANY WARRANTY; without the implied warranty  of  MERCHANTABILITY *
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License *
 * for more details.                                                       *
 *                                                                         *
 * You should have received a copy of the GNU General Public License along *
 * with this program; if not, write to the Free Software Foundation, Inc., *
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA              *
 ***************************************************************************
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/stat.h>

#define PROC_DENTRY_STATE       "/proc/sys/fs/dentry-state"
#define ENV_TIME_FMT            "S_TIME_FORMAT"
#define VERSION                 "0.1"
#define ONE_MINUTE              60      /* seconds */
#define prog                    "dentry-state"

struct dentry_stat {
        int nr_dentry;
        int nr_unused;
        int age_limit;          /* age in seconds */
        int want_pages;         /* pages requested by system */
        int nr_negative;        /* # of unused negative dentries */
        int dummy;              /* Reserved for future use */
        time_t rectime;         /* Record time */
};

/* start time & now timestamp */
time_t st, now;                 
/* string of current time */
char curr_time[64];             
/* convert second to time by localtime */
struct tm tm;                   
/* store curr & prev stat */
struct dentry_stat stats[2];    
struct dentry_stat *curr, *prev;
/* current index of stats */
int curr_idx = 0;               
/* stat info on start */
struct dentry_stat init_stat;           

/* set to 1 when received int signal */
int sig_exit = 0;               
/* flag of print header or no */
int header = 1;                 

/*
 * read_dentry_stat -- Read dentry from procfs
 *   @stat    store dentry stat data
 *
 * Return 0 if success, otherwise -1.
 *
 */
int read_dentry_stat(struct dentry_stat *stat)
{
        int fd;
        char buf[80];
        int ret = -1;
        int nr_filed;
        static int first = 1;

        if (!stat)
                return -1;

        fd = open(PROC_DENTRY_STATE, O_RDONLY);
        if (fd < 0)
                return -1;

        memset(buf, 0, sizeof(buf));
        if (read(fd, buf, sizeof(buf) - 1) <= 0)
                goto fail;

        nr_filed = sscanf(buf, "%d\t%d\t%d\t%d\t%d\t%d\n",
                          &(stat->nr_dentry), &(stat->nr_unused),
                          &(stat->age_limit), &(stat->want_pages),
                          &(stat->nr_negative), &(stat->dummy));
        if (nr_filed != 6)
                goto fail;

        if (time(&(stat->rectime)) < 0)
                goto fail;

        if (first) {
                first = 0;
                memcpy(&init_stat, curr, sizeof(init_stat));
        }

        ret = 0;

      fail:
        close(fd);
        return ret;
}

/*
 * usage -- print usage and exit
 */
void usage(void)
{
        fprintf(stderr, "Usage: %s [ <interval> [ <count> ] ]\n", prog);

        exit(1);
}

/*
 * write_header -- print header to console.
 */
void write_header(void)
{
        strftime(curr_time, sizeof(curr_time), "%r", &tm);
        printf("\n%s\t%22s\t%22s\t%22s\n", curr_time, "NR_dentry[+/-]",
               "NR_unused[+/-]", "NR_negative[+/-]");
}

/*
 * write_data -- write dentry data to console.
 */
void write_data(const char *prefix, struct dentry_stat *curr,
                 struct dentry_stat *prev)
{
        printf("%-11s\t%10d[%10d]\t%10d[%10d]\t%10d[%10d]\n", prefix,
               curr->nr_dentry, curr->nr_dentry - prev->nr_dentry,
               curr->nr_unused, curr->nr_unused - prev->nr_unused,
               curr->nr_negative, curr->nr_negative - prev->nr_negative);
}

/*
 * int_handler -- signal SIGINT handler, set sig_exit, main will process it.
 */
void int_handler(int sig)
{
        sig_exit = 1;
}

/*
 * alarm_handler -- signal SIGALRM handler, it used to print header.
 */
void alarm_handler(int sig)
{
        header = 1;
        alarm(ONE_MINUTE);
}

/*
 * write_statistic -- print statistic info, be called on exit.
 */
void write_statistic(void)
{
        prev = &init_stat;

        printf
            ("\n\n-------------------- [ S T A T I S T I C ] --------------------\n");
        printf("%20s: %lu(s)\n", "Duration", time(NULL) - st);
        printf("%20s: %d\n", "NR_dentry",
               curr->nr_dentry - prev->nr_dentry);
        printf("%20s: %d\n", "NR_unused",
               curr->nr_unused - prev->nr_unused);
        printf("%20s: %d\n", "NR_negative",
               curr->nr_negative - prev->nr_negative);
        printf("\n\n");
}

int main(int argc, char **argv)
{
        int interval, total;
        struct utsname utsname;
        struct sigaction int_act, alarm_act;

        if (argv[1]) {
                interval = atoi(argv[1]);
                if (interval <= 0) {
                        fprintf(stderr, "Invalid interval!\n");
                        usage();
                }
        }

        if (argv[2]) {
                total = atoi(argv[2]);
                if (total <= 0) {
                        total = 1;
                }
        } else if (interval > 0)
                total = 100;
        else
                total = 1;

        if (uname(&utsname) < 0) {
                fprintf(stderr, "Failed to get utsname!\n");
                return -1;
        }

        /* Set a handler for SIGINT */
        memset(&int_act, 0, sizeof(int_act));
        int_act.sa_handler = int_handler;
        sigaction(SIGINT, &int_act, NULL);

        /* Set a handler for SIGALRM */
        memset(&alarm_act, 0, sizeof(alarm_act));
        alarm_act.sa_handler = alarm_handler;
        sigaction(SIGALRM, &alarm_act, NULL);

        /* Get start time */
        st = now = time(NULL);
        localtime_r(&now, &tm);
        strftime(curr_time, sizeof(curr_time), "%D", &tm);

        /* Print systme info */
        printf("\n%s %s (%s)\t%s\t_%s_\t%s-%s\n", utsname.sysname,
               utsname.release, utsname.nodename, curr_time,
               utsname.machine, prog, VERSION);
        memset(&stats, 0, sizeof(stats));

        while (1) {
                if (sig_exit)
                        break;

                curr = &stats[curr_idx];
                prev = &stats[!curr_idx];
                curr_idx = !curr_idx;

                memset(curr, 0, sizeof(*curr));
                if (read_dentry_stat(curr) < 0) {
                        fprintf(stderr, "%s: %s\n", argv[0],
                                strerror(errno));
                        return -1;
                }

                localtime_r(&(curr->rectime), &tm);
                strftime(curr_time, sizeof(curr_time), "%r", &tm);

                if (header) {
                        header = 0;
                        write_header();
                        memset(prev, 0, sizeof(*prev));
                        alarm(ONE_MINUTE);
                }

                write_data(curr_time, curr, prev);

                if (--total <= 0)
                        break;
                sleep(interval);
        }

        write_statistic();

        return 0;
}
