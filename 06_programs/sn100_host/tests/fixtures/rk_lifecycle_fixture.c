/* Native-only lifecycle fixture. This does not exercise Qt, ARM64 or RK. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/prctl.h>

static volatile sig_atomic_t stop_requested;

static void request_stop(int signal_number)
{
    stop_requested = signal_number;
}

static void pause_ms(long milliseconds)
{
    struct timespec delay = { milliseconds / 1000, (milliseconds % 1000) * 1000000 };
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR && !stop_requested) {}
}

int main(int argc, char **argv)
{
    const char *mode = getenv("RK_LIFECYCLE_MODE");
    const char *secret_path = NULL;
    const char *data_directory = NULL;
    struct sigaction action;
    long elapsed_ms = 0;
    /* /proc/stat's comm field may contain spaces and closing parentheses. */
    if (prctl(PR_SET_NAME, "fixture) worker", 0, 0, 0) != 0) return 69;
    if (!mode || !*mode) mode = "normal";
    for (int index = 1; index < argc; ++index)
    {
        if (strcmp(argv[index], "--mode") == 0 && index + 1 < argc) mode = argv[++index];
        else if (strcmp(argv[index], "--test-secret-file") == 0 && index + 1 < argc) secret_path = argv[++index];
        else if (strcmp(argv[index], "--data-dir") == 0 && index + 1 < argc) data_directory = argv[++index];
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) != 0 || sigaction(SIGINT, &action, NULL) != 0) return 70;
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (secret_path)
    {
        FILE *secret = fopen(secret_path, "r");
        if (!secret) return 71;
        if (fgetc(secret) == EOF) { fclose(secret); return 72; }
        fclose(secret);
    }
    printf("FIXTURE_NATIVE_ONLY pid=%ld mode=%s data_supplied=%d\n", (long)getpid(), mode, data_directory != NULL);
    if (strcmp(mode, "early-failure") == 0) return 29;
    if (strcmp(mode, "early-clean") == 0) return 0;
    if (strcmp(mode, "active-log") == 0)
    {
        char padding[4096];
        memset(padding, 'A', sizeof(padding) - 1);
        padding[sizeof(padding) - 1] = '\0';
        for (int index = 0; index < 256; ++index) puts(padding);
    }
    puts("SHELL_READY");
    for (;;)
    {
        if (stop_requested)
        {
            puts("SHUTDOWN_REQUEST fixture_native_only");
            stop_requested = 0;
            if (strcmp(mode, "reject-stop") == 0) puts("SHUTDOWN_REJECTED fixture_guard");
            else break;
        }
        if (strcmp(mode, "normal") == 0 && elapsed_ms >= 800) break;
        if (strcmp(mode, "nonzero") == 0 && elapsed_ms >= 800) return 23;
        if (strcmp(mode, "active-log") == 0) printf("FIXTURE_ACTIVE_LOG elapsed_ms=%ld\n", elapsed_ms);
        pause_ms(20);
        elapsed_ms += 20;
    }
    puts("SHUTDOWN_ACCEPTED fixture_native_only");
    puts("HOSPITAL_STORE_CLOSED");
    puts("ACCOUNT_STORE_CLOSED");
    puts("HISTORY_STORE_CLOSED");
    puts("THREADS_STOPPED true");
    puts("EXIT code= 0 fixture_native_only");
    return 0;
}
