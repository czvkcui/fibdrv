#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define FIB_DEV "/dev/fibonacci"

#define BILLION 1e9
#define LOG_FILE "log.txt"

struct timespec diff(struct timespec start, struct timespec end)
{
    struct timespec temp;
    if ((end.tv_nsec - start.tv_nsec) < 0) {
        temp.tv_sec = end.tv_sec - start.tv_sec - 1;
        temp.tv_nsec = BILLION + end.tv_nsec - start.tv_nsec;
    } else {
        temp.tv_sec = end.tv_sec - start.tv_sec;
        temp.tv_nsec = end.tv_nsec - start.tv_nsec;
    }
    return temp;
}

int main()
{
    int fd;
    long long sz;

    char buf[1];
    char write_buf[] = "testing writing";
    int offset = 100;  // TODO: test something bigger than the limit
    int i = 0;
    FILE *fp = fopen(LOG_FILE, "w");
    char buffer[100];
    char kbuff[100];

    struct timespec time1, time2;

    fd = open(FIB_DEV, O_RDWR);

    if (fd < 0) {
        perror("Failed to open character device");
        exit(1);
    }

    for (i = 0; i <= offset; i++) {
        sz = write(fd, write_buf, strlen(write_buf));
        printf("Writing to " FIB_DEV ", returned the sequence %lld\n", sz);
    }

    char *start = "N kernel_space_time(ns) user_space_time(sec)\n";
    fwrite(start, sizeof(char), strlen(start), fp);
    for (i = 0; i <= offset; i++) {
        lseek(fd, i, SEEK_SET);
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time1);
        sz = read(fd, kbuff, 1);
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time2);
        printf("Reading from " FIB_DEV
               " at offset %d, returned the sequence "
               "%lld.\n",
               i, sz);
        struct timespec tsec = diff(time1, time2);
        sprintf(buffer, "%d %s %ld.%ld\n", i, kbuff, tsec.tv_sec, tsec.tv_nsec);
        fwrite(buffer, sizeof(char), strlen(buffer), fp);
    }

    for (i = offset; i >= 0; i--) {
        lseek(fd, i, SEEK_SET);
        sz = read(fd, buf, 1);
        printf("Reading from " FIB_DEV
               " at offset %d, returned the sequence "
               "%lld.\n",
               i, sz);
    }

    close(fd);
    fclose(fp);
    return 0;
}
