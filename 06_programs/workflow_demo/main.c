#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <unistd.h>

static int parse_number(const char *text, long *value)
{
    char *end;
    errno = 0;
    *value = strtol(text, &end, 10);
    return errno == 0 && end != text && *end == '\0' &&
           *value >= -1000000 && *value <= 1000000;
}

int main(int argc, char **argv)
{
    struct utsname system_info;
    char *directory;
    long left = 12, right = 30;

    if (argc != 1 && argc != 3) {
        fprintf(stderr, "Usage: %s [INTEGER INTEGER]\n", argv[0]);
        return 2;
    }
    if (argc == 3 &&
        (!parse_number(argv[1], &left) || !parse_number(argv[2], &right))) {
        fprintf(stderr, "Expected two integers in [-1000000, 1000000].\n");
        return 2;
    }
    if (uname(&system_info) != 0) {
        perror("uname");
        return 1;
    }
    directory = getcwd(NULL, 0);
    if (directory == NULL) {
        perror("getcwd");
        return 1;
    }

    printf("Hello from the RK3576 AI workflow!\n");
    printf("HOST=%s\nARCH=%s\nCWD=%s\n", system_info.nodename,
           system_info.machine, directory);
    printf("INPUT left=%ld right=%ld\n", left, right);
    printf("OUTPUT %ld + %ld = %ld\n", left, right, left + right);
    free(directory);
    return 0;
}
