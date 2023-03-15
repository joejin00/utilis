/*
 * mpstat2numa.c -- covert mpstat workload to numa node view
 *
 * Compile:  gcc -Wall -o mpstat2numa mpstat2numa.c
 *
 * By: Joe Jin <joe.jin@oracle.com>
 *
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <libgen.h>

/*
 * CPU Topology:
 *
 * NUMA node0 CPU(s):     0-27,224-251
 * NUMA node1 CPU(s):     28-55,252-279
 * NUMA node2 CPU(s):     56-83,280-307
 * NUMA node3 CPU(s):     84-111,308-335
 * NUMA node4 CPU(s):     112-139,336-363
 * NUMA node5 CPU(s):     140-167,364-391
 * NUMA node6 CPU(s):     168-195,392-419
 * NUMA node7 CPU(s):     196-223,420-447
 */
#define NR_CORE 28
#define NR_NODE 8
#define NR_THREAD 2
#define NR_CPU_THREAD (NR_CORE * NR_THREAD)
#define CPU2NUMA(cpu) (((cpu) / NR_CORE) % NR_NODE)

#ifndef PATH_MAX
#define PATH_MAX        80
#endif

#define NR_HLINES       20      /* print header after print data lines */

#define MPSTAT_HEAD_GNICE "CPU    %usr   %nice    %sys %iowait    %irq   %soft  %steal  %guest  %gnice   %idle"
#define MPSTAT_HEAD "CPU    %usr   %nice    %sys %iowait    %irq   %soft  %steal  %guest   %idle"

#define MPSTAT_SCAN_FMT_GNICE   "%s %d %f %f %f %f %f %f %f %f %f %f\n"
#define MPSTAT_SCAN_FMT         "%s %d %f %f %f %f %f %f %f %f %f\n"

char *prog;

int gnice = 0;                  /* Has gnice? */
int usr_flag = 0;               /* print %usr only */
int nice_flag = 0;              /* print %nice only */
int sys_flag = 0;               /* print %sys only */
int iowait_flag = 0;            /* print %iowait only */
int irq_flag = 0;               /* print %irq only */
int soft_flag = 0;              /* print %soft only */
int steal_flag = 0;             /* print %steal only */
int guest_flag = 0;             /* print %guest only */
int gnice_flag = 0;             /* print %gnice only */
int idle_flag = 0;              /* print %idle only */
int util_flag = 0;              /* print 100 - idle */
int print_all = 1;              /* print all nodes stat */
int header_flag = 1;            /* print header or no? */
int nowarn_flag = 0;            /* print warn on error? */
int print_fields = 0;           /* number fields to be printed */
int node = -1;                  /* The node to print the stat */
int cpu = -1;                   /* CPU list to print stat */

int max_files = 0;

struct numa_stat {
        unsigned int cpu;
        char time[32];
        float usr;
        float nice;
        float sys;
        float iowait;
        float irq;
        float soft;
        float steal;
        float guest;
        float gnice;
        float idle;
};

struct numa_stat stats[NR_NODE];

void print_stat_multi(struct numa_stat s[NR_NODE])
{
        int i;
        static int print_lines = 0;

#define printf_field(f) fprintf(stdout, "%8s", f)
        if (header_flag && print_lines % NR_HLINES == 0) {
                fprintf(stdout, "\n%-13s%4s", "TIME", "NODE");
                if (print_all || usr_flag)      printf_field("%usr");
                if (print_all || nice_flag)     printf_field("%nice");
                if (print_all || sys_flag)      printf_field("%sys");
                if (print_all || iowait_flag)   printf_field("iowait");
                if (print_all || irq_flag)      printf_field("%irq");
                if (print_all || soft_flag)     printf_field("%soft");
                if (print_all || steal_flag)    printf_field("%steal");
                if (print_all || guest_flag)    printf_field("%guest");
                if (gnice && (print_all || gnice_flag)) printf_field("%gnice");
                if (print_all || idle_flag)     printf_field("%idle");
                if (util_flag)                  printf_field("%util");
                fprintf(stdout, "\n");
        }

#define printf_value(member) fprintf(stdout, "%8.2f", (&s[i])->member / NR_CPU_THREAD)

        for (i = 0; i < NR_NODE; i++) {
                if (node != -1 && i != node) continue;
                print_lines++;
                fprintf(stdout, "%-13s%4d", s[i].time, i);
                if (print_all || usr_flag)      printf_value(usr);
                if (print_all || nice_flag)     printf_value(nice);
                if (print_all || sys_flag)      printf_value(sys);
                if (print_all || iowait_flag)   printf_value(iowait);
                if (print_all || irq_flag)      printf_value(irq);
                if (print_all || soft_flag)     printf_value(soft);
                if (print_all || steal_flag)    printf_value(steal);
                if (print_all || guest_flag)    printf_value(guest);
                if (gnice && (print_all || gnice_flag)) printf_value(gnice);
                if (print_all || idle_flag)     printf_value(idle);
                if (util_flag)
                        fprintf(stdout, "%8.2f",
                                (100.00 - s[i].idle / NR_CPU_THREAD));
        }
        if (header_flag)
                fprintf(stdout, "\n");
        memset(s, 0, sizeof(stats));
}

void print_stat_util(struct numa_stat s[NR_NODE])
{
        int i;
        static int header = 0;

        header++;
        if (header_flag && header % NR_HLINES == 0) {
                fprintf(stdout, "\n%-13s", "UTIL_TIME");
                for (i = 0; i < NR_NODE; i++)
                        fprintf(stdout, "%6s%02d ", "N", i);
                fprintf(stdout, "\n");
        }
        fprintf(stdout, "%-12s", s[0].time);
        for (i = 0; i < NR_NODE; i++)
                fprintf(stdout, "%9.2f",
                        (100.00 - s[i].idle / NR_CPU_THREAD));
        fprintf(stdout, "\n");
        memset(s, 0, sizeof(stats));
}

#define define_print_stat(name, s) \
  void print_stat_##name(struct numa_stat s[NR_NODE]) {                 \
        int i;                                                          \
        static int header = 0;                                          \
        header++;                                                       \
        if (header_flag && header % NR_HLINES  == 0) {                  \
                fprintf(stdout, "\n%-13s", #name"-TIME");               \
                for (i = 0; i < NR_NODE; i++)                           \
                        fprintf(stdout, "%6s%02d ", "N", i);            \
                fprintf(stdout, "\n");                                  \
        }                                                               \
        fprintf(stdout, "%-12s", s[0].time);                            \
        for (i = 0; i < NR_NODE; i++)                                   \
                fprintf(stdout, "%9.2f", s[i].name / NR_CPU_THREAD);    \
        fprintf(stdout, "\n");                                          \
        memset(s, 0, sizeof(stats));                                    \
}

define_print_stat(usr, s)
define_print_stat(nice, s)
define_print_stat(sys, s)
define_print_stat(iowait, s)
define_print_stat(irq, s)
define_print_stat(soft, s)
define_print_stat(steal, s)
define_print_stat(guest, s)
define_print_stat(idle, s)
define_print_stat(gnice, s)

void print_numa_stat(struct numa_stat s[NR_NODE])
{
        static int first = 0;

        /* Don't print for first run */
        if (first == 0) {
                first++;
                return;
        }

        if (print_all || print_fields > 1) {
                print_stat_multi(s);
                return;
        }
#define check_print_and_return(field) do {      \
        if (field ## _flag) {                   \
                print_stat_##field(s);          \
                return;                         \
        }                                       \
} while (0)

        check_print_and_return(usr);
        check_print_and_return(nice);
        check_print_and_return(sys);
        check_print_and_return(iowait);
        check_print_and_return(irq);
        check_print_and_return(soft);
        check_print_and_return(steal);
        check_print_and_return(guest);
        check_print_and_return(idle);
        check_print_and_return(util);
        if (gnice_flag) {
                if (gnice) {
                        print_stat_gnice(s);
                        return;
                }
                fprintf(stdout, "No gnice included by mpstat!\n");
                exit(0);
        }

        return;
}

void add_numa_stat(struct numa_stat *to, struct numa_stat from)
{
        strncpy(to->time, from.time, 32);

        to->usr += from.usr;
        to->nice += from.nice;
        to->sys += from.sys;
        to->iowait += from.iowait;
        to->irq += from.irq;
        to->soft += from.soft;
        to->steal += from.steal;
        to->guest += from.guest;
        to->idle += from.idle;
        if (gnice)
                to->gnice += from.gnice;
}


int process_one(const char *fn)
{

        FILE *fp = NULL;
        char *line = NULL;
        size_t len = 0;
        ssize_t read;
        struct numa_stat tmp_stat;
        int print_lines = 0;


        fp = fopen(fn, "r");
        if (fp == NULL) {
                if (nowarn_flag == 0)
                        fprintf(stderr, "Warning: Failed to open file %s\n", fn);
                return -1;
        }

        while ((read = getline(&line, &len, fp)) != -1) {
                /* Empty line */
                if (strlen(line) < 5)   /* Empty line? */
                        continue;

                /* Average line, skip */
                if (strncmp(line, "Average:", strlen("Average:")) == 0)
                        continue;

                /* Not start with time */
                if (line[0] != '0' && line[0] != '1' && line[0] != '2')
                        continue;

                /* output include gnice? */
                if (strstr(line, "%gnice") != NULL)
                        gnice = 1;

                if (strstr(line, "  all   ") != NULL)
                        continue;

                if (strstr(line, (gnice ? MPSTAT_HEAD_GNICE : MPSTAT_HEAD))
                    != NULL) {
                        /* Only want to get given CPU stat */
                        if (cpu >= 0) {
                                if (header_flag == 1 &&
                                    print_lines % NR_HLINES == 0)
                                        fprintf(stdout, "\n%s", line);
                                continue;
                        }
                        print_numa_stat(stats);
                }

                memset(&tmp_stat, 0, sizeof(tmp_stat));
                if (gnice)
                        sscanf(line, MPSTAT_SCAN_FMT_GNICE,
                               tmp_stat.time,
                               &tmp_stat.cpu,
                               &tmp_stat.usr,
                               &tmp_stat.nice,
                               &tmp_stat.sys,
                               &tmp_stat.iowait,
                               &tmp_stat.irq,
                               &tmp_stat.soft,
                               &tmp_stat.steal,
                               &tmp_stat.guest,
                               &tmp_stat.gnice, &tmp_stat.idle);
                else
                        sscanf(line, MPSTAT_SCAN_FMT,
                               tmp_stat.time,
                               &tmp_stat.cpu,
                               &tmp_stat.usr,
                               &tmp_stat.nice,
                               &tmp_stat.sys,
                               &tmp_stat.iowait,
                               &tmp_stat.irq,
                               &tmp_stat.soft,
                               &tmp_stat.steal,
                               &tmp_stat.guest, &tmp_stat.idle);
                if (cpu != -1 && tmp_stat.cpu == cpu) {
                        print_lines++;
                        fprintf(stdout, "%s", line);
                        continue;
                }
                add_numa_stat(&stats[CPU2NUMA(tmp_stat.cpu)], tmp_stat);
        }

        free(line);
        fclose(fp);

        return 0;
}


void usage_and_exit(char *prog, int exit_code, const char *fmt, ...)
{
        va_list args;

        fprintf(stderr, "%s: ", prog);
        /* Print error message */
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);

        fprintf(stderr, "%s --  Convert mpstat out to NUMA node workload\n\n",
                         prog);
        fprintf(stderr, "Usage: %s -noheader -nowarn -usr -nice -sys"
                        " -iowait -irq -soft -steal -guest -idle -util"
                        " file1 file2 ...\n\n", prog);
        fprintf(stderr, "       -noheader : Don't print header\n");
        fprintf(stderr, "       -nowarn   : Don't print warning message\n");
        fprintf(stderr, "       -help|-h  : Print this help\n");
        fprintf(stderr, "       -usr      : print %%usr of all nodes\n");
        fprintf(stderr, "       -nice     : print %%nice of all nodes\n");
        fprintf(stderr, "       -sys      : print %%sys of all nodes\n");
        fprintf(stderr, "       -iowait   : print %%iowait of all nodes\n");
        fprintf(stderr, "       -irq      : print %%irq of all nodes\n");
        fprintf(stderr, "       -soft     : print %%soft of all nodes\n");
        fprintf(stderr, "       -steal    : print %%steal of all nodes\n");
        fprintf(stderr, "       -guest    : print %%guest of all nodes\n");
        fprintf(stderr, "       -idle     : print %%idle of all nodes\n");
        fprintf(stderr, "       -util     : print %%(100-idle) of all nodes\n");
        fprintf(stderr, "       -node n   : print given node stat only\n");
        fprintf(stderr, "       -cpu n    : print given cpu only\n");
        fprintf(stderr, "\n\n");

        exit(exit_code);
}

#define error_exit(condition, exit_code, fmt, ...) do {                 \
        if ((condition)) {                                              \
                usage_and_exit(prog, exit_code, fmt, ##__VA_ARGS__);    \
        }                                                               \
} while(0)

int validate_number(const char *s, int min, int max)
{
        int i, ret;

        for (i = 0; i < strlen(s); i++) {
                if (!isdigit(s[i]))
                        return -1;
        }
        ret = atoi(s);
        if (ret > max || ret < min)
                return -1;
        return ret;
}

int main(int argc, char **argv)
{
        int i, good, bad;
        char **files;

        prog = basename(argv[0]);

        error_exit((argc < 2), EXIT_FAILURE,
                   "%s: not enough arguments", prog);

        files = (char **) malloc(argc * (sizeof(void *)));
        if (files == NULL) {
                fprintf(stderr, "No memory!\n");
                exit(EXIT_FAILURE);
        }
#define check_and_set_flag(f)                   \
        if (strcmp(argv[i], "-" #f) == 0) {     \
                f ## _flag = 1;                 \
                print_all = 0;                  \
                print_fields++;                 \
                continue;                       \
        }

        for (i = 1; i < argc; i++) {
                check_and_set_flag(usr);
                check_and_set_flag(nice);
                check_and_set_flag(sys);
                check_and_set_flag(iowait);
                check_and_set_flag(irq);
                check_and_set_flag(soft);
                check_and_set_flag(steal);
                check_and_set_flag(guest);
                check_and_set_flag(gnice);
                check_and_set_flag(idle);
                check_and_set_flag(util);

                if (strcmp(argv[i], "-noheader") == 0) {
                        header_flag = 0;
                        continue;
                }

                if (strcmp(argv[i], "-nowarn") == 0) {
                        nowarn_flag = 0;
                        continue;
                }

                if (strcmp(argv[i], "-node") == 0) {
                        error_exit(argc < i + 2, EXIT_FAILURE,
                                   "[ERROR]: No node given\n\n");
                        i++;
                        node = validate_number(argv[i], 0, NR_NODE - 1);
                        error_exit(node < 0, EXIT_FAILURE,
                                  "[ERROR]: Invalid Node %s, range: [0-%d]\n\n",
                                  argv[i], NR_NODE - 1);
                        continue;
                }

                if (strcmp(argv[i], "-cpu") == 0) {
                        error_exit(argc < i+2, EXIT_FAILURE,
                                   "[ERROR]: No cpu given!\n\n");

                        i++;
                        cpu = validate_number(argv[i], 0, NR_NODE * NR_CPU_THREAD - 1);
                        error_exit(cpu < 0, EXIT_FAILURE,
                                   "[ERROR]: Invalid cpu %s, range: [0-%d]\n\n",
                                   argv[i], NR_NODE * NR_CPU_THREAD - 1);
                        continue;
                }

                if (strcmp(argv[i], "-help") == 0 ||
                    strcmp(argv[i], "--help") == 0 ||
                    strcmp(argv[i], "-h") == 0) {
                        error_exit(1, 0, "");
                }

                files[max_files] = (char *) malloc(PATH_MAX);
                if (files[max_files] == NULL) {
                        perror("malloc(): ");
                        exit(EXIT_FAILURE);
                }
                strncpy(files[max_files], argv[i], PATH_MAX - 1);
                max_files++;
        }
        error_exit(max_files == 0, EXIT_FAILURE, "ERROR: No input file!\n\n");

        if (header_flag) {
                fprintf(stdout, "-----------------------------\n");
                fprintf(stdout, "Thread(s) per core : %d\n", NR_THREAD);
                fprintf(stdout, "NUMA node(s)       : %d\n", NR_NODE);
                fprintf(stdout, "Core(s) per socket : %d\n", NR_CORE);
                fprintf(stdout, "-----------------------------\n\n");
        }

        good = bad = 0;
        for (i = 0; i < max_files; i++) {
                if (process_one(files[i]))
                        bad++;
                else
                        good++;
                free(files[i]);
        }
        free(files);

        if (header_flag)
                fprintf(stdout, "\n\n[INFO]: Inputs: %d, success: %d, failed: %d.\n\n",
                                max_files, good, bad);

        return 0;
}
