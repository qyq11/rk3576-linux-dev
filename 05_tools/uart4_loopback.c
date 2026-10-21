/* RK3576 Linux UART loopback, default /dev/ttyS4, 115200 8N1.
 * Build on RK: gcc -std=c11 -O2 -Wall -Wextra -Wpedantic uart4_loopback.c -o uart4_loopback
 * Connect physical pins 8 (TX) and 10 (RX) only after checking the board pinout.
 * This tests a local loopback; Windows-to-RK communication is a separate test.
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t interrupted;

static void on_signal(int signal_number)
{
    (void)signal_number;
    interrupted = 1;
}

static int64_t now_ms(void)
{
    struct timespec timestamp;
    if (clock_gettime(CLOCK_MONOTONIC, &timestamp) < 0)
        return -1;
    return (int64_t)timestamp.tv_sec * 1000 + timestamp.tv_nsec / 1000000;
}

/* Return 1 when ready, 0 on timeout, -1 on interruption or I/O error. */
static int wait_until(int fd, short events, int64_t deadline)
{
    struct pollfd descriptor = {.fd = fd, .events = events};
    while (!interrupted) {
        int64_t current = now_ms();
        if (current < 0)
            return -1;
        int64_t remaining = deadline - current;
        if (remaining <= 0)
            return 0;
        int timeout = remaining > INT_MAX ? INT_MAX : (int)remaining;
        int result = poll(&descriptor, 1, timeout);
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0)
            return result;
        if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            errno = EIO;
            return -1;
        }
        if (descriptor.revents & events)
            return 1;
    }
    errno = EINTR;
    return -1;
}

static int write_frame(int fd, const unsigned char *data, size_t length,
                       size_t *written)
{
    int64_t start = now_ms();
    *written = 0;
    if (start < 0)
        return -1;
    while (*written < length) {
        int ready = wait_until(fd, POLLOUT, start + 1000);
        if (ready != 1) {
            if (ready == 0)
                errno = ETIMEDOUT;
            return -1;
        }
        ssize_t count = write(fd, data + *written, length - *written);
        if (count < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (count <= 0) {
            if (count == 0)
                errno = EIO;
            return -1;
        }
        *written += (size_t)count;
    }
    return 0;
}

static int read_frame(int fd, unsigned char *data, size_t capacity,
                      size_t expected, size_t *received)
{
    int64_t start = now_ms();
    *received = 0;
    if (start < 0)
        return -1;
    while (*received < expected) {
        int ready = wait_until(fd, POLLIN, start + 1000);
        if (ready <= 0)
            return ready; /* A timeout is reported by the subsequent comparison. */
        ssize_t count = read(fd, data + *received, capacity - *received);
        if (count < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (count <= 0) {
            if (count == 0)
                errno = EIO;
            return -1;
        }
        *received += (size_t)count;
    }
    return 0;
}

static void show_frame(const char *direction, unsigned sequence,
                       const unsigned char *data, size_t length)
{
    printf("%s [%04u] %zu bytes: \"", direction, sequence, length);
    for (size_t i = 0; i < length; ++i) {
        unsigned char value = data[i];
        if (value == '\r')
            fputs("\\r", stdout);
        else if (value == '\n')
            fputs("\\n", stdout);
        else if (value == '\\' || value == '"')
            printf("\\%c", (int)value);
        else if (value >= 32 && value <= 126)
            putchar((int)value);
        else
            printf("\\x%02X", (unsigned)value);
    }
    fputs("\"  HEX:", stdout);
    for (size_t i = 0; i < length; ++i)
        printf(" %02X", (unsigned)data[i]);
    putchar('\n');
}

static void usage(const char *program)
{
    printf("Usage: %s [--frames 1..9999] [--port /dev/ttyS4]\n", program);
    puts("Default: 100 frames; 115200, 8N1, no flow control; prints actual TX/RX bytes.");
}

int main(int argc, char **argv)
{
    const char *port = "/dev/ttyS4";
    unsigned frames = 100, matched = 0;
    size_t sent_total = 0, received_total = 0;
    int exit_code = 2, fd = -1;
    bool saved_valid = false;
    struct termios saved, settings;
    unsigned char received[4096];
    struct sigaction action = {0};

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            char *end;
            errno = 0;
            long value = strtol(argv[++i], &end, 10);
            if (errno || *end || value < 1 || value > 9999) {
                fputs("--frames must be between 1 and 9999\n", stderr);
                return 2;
            }
            frames = (unsigned)value;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (strcmp(port, "/dev/ttyFIQ0") == 0 || strcmp(port, "/dev/ttyS6") == 0) {
        fputs("Refusing this project's debug console or Bluetooth UART.\n", stderr);
        return 2;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) < 0 || sigaction(SIGTERM, &action, NULL) < 0) {
        perror("sigaction");
        return 2;
    }
    fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror(port);
        return 2;
    }
    if (ioctl(fd, TIOCEXCL) < 0 || tcgetattr(fd, &saved) < 0) {
        perror("claim UART / read settings");
        goto cleanup;
    }
    saved_valid = true;
    settings = saved;
    cfmakeraw(&settings);
    settings.c_cflag &= ~(CSIZE | PARENB | CSTOPB);
#ifdef CRTSCTS
    settings.c_cflag &= ~CRTSCTS;
#endif
    settings.c_cflag |= CS8 | CLOCAL | CREAD;
    settings.c_cc[VMIN] = 0;
    settings.c_cc[VTIME] = 0;
    if (cfsetispeed(&settings, B115200) < 0 || cfsetospeed(&settings, B115200) < 0 ||
        tcsetattr(fd, TCSANOW, &settings) < 0 || tcflush(fd, TCIOFLUSH) < 0) {
        perror("configure UART");
        goto cleanup;
    }
    printf("Port: %s; settings: 115200 8N1; flow control: none\n", port);
    for (unsigned sequence = 1; sequence <= frames && !interrupted; ++sequence) {
        unsigned char payload[32];
        int length = snprintf((char *)payload, sizeof(payload), "UART_TEST_%04u\r\n", sequence);
        size_t written = 0, count = 0;
        if (length < 0 || (size_t)length >= sizeof(payload)) {
            fputs("Unable to format test frame\n", stderr);
            goto cleanup;
        }
        int status = write_frame(fd, payload, (size_t)length, &written);
        int io_error = errno;
        sent_total += written;
        show_frame("TX", sequence, payload, written);
        if (status < 0) {
            errno = io_error;
            perror("UART write");
            goto cleanup;
        }
        status = read_frame(fd, received, sizeof(received), (size_t)length, &count);
        io_error = errno;
        received_total += count;
        show_frame("RX", sequence, received, count);
        if (status < 0) {
            errno = io_error;
            perror("UART read");
            goto cleanup;
        }
        if (count != (size_t)length || memcmp(received, payload, (size_t)length) != 0) {
            printf("Mismatch at frame %u\n", sequence);
            exit_code = 1;
            goto cleanup;
        }
        ++matched;
    }
    if (interrupted)
        goto cleanup;
    int64_t current = now_ms();
    if (current < 0) {
        perror("clock_gettime");
        goto cleanup;
    }
    int ready = wait_until(fd, POLLIN, current + 100);
    if (ready < 0) {
        perror("check trailing data");
        goto cleanup;
    }
    if (ready == 1) {
        ssize_t count = read(fd, received, sizeof(received));
        if (count < 0) {
            perror("read trailing data");
            goto cleanup;
        }
        if (count > 0) {
            received_total += (size_t)count;
            show_frame("EXTRA", 0, received, (size_t)count);
            exit_code = 1;
            goto cleanup;
        }
    }
    exit_code = 0;

cleanup:
    if (saved_valid && tcsetattr(fd, TCSANOW, &saved) < 0) {
        perror("restore UART settings");
        exit_code = 2;
    }
    if (fd >= 0) {
        (void)ioctl(fd, TIOCNXCL);
        close(fd);
    }
    if (interrupted)
        exit_code = 130;
    printf("Sent bytes: %zu\nReceived bytes: %zu\nMatched frames: %u/%u\n",
           sent_total, received_total, matched, frames);
    printf("Result: %s\n", exit_code == 0 ? "PASS" :
           exit_code == 1 ? "FAIL" : exit_code == 130 ? "INTERRUPTED" : "ERROR");
    return exit_code;
}
