/* TaishanPi 3M RK3576: hold one 40-pin header GPIO for multimeter checks.
 * API: libgpiod 1.x (board currently has 1.6.3).
 * Build on RK: gcc -std=c11 -O2 -Wall -Wextra -Wpedantic gpio_output.c -o gpio_output -lgpiod
 * Run on RK:   sudo ./gpio_output 13
 * Commands: 0 + Enter = LOW, 1 + Enter = HIGH, q + Enter = release and exit.
 * Disconnect loads and verify header orientation before the first test.
 * Printed values are software settings; measure voltage relative to GND.
 */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <gpiod.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct pin_mapping {
    unsigned int physical;
    unsigned int bank;
    unsigned int offset;
    const char *gpio;
    const char *purpose;
};

/* Physical pinout and project allocation; purpose does not imply an LED exists.
 * Verify gpiochip labels below against the running board before requesting.
 */
static const struct pin_mapping mappings[] = {
    {13, 2, 22, "GPIO2_C6", "LED-R"},
    {15, 0, 23, "GPIO0_C7", "LED-G"},
    {16, 0, 22, "GPIO0_C6", "LED-B"},
    {18, 3,  3, "GPIO3_A3", "LED-E"},
    {22, 2, 30, "GPIO2_D6", "LED-P"},
    {36, 2, 31, "GPIO2_D7", "LED-D"},
    {38, 3,  2, "GPIO3_A2", "Run"},
};

static volatile sig_atomic_t interrupted;

static void on_signal(int number)
{
    interrupted = number;
}

static void usage(const char *program)
{
    printf("Usage: %s PHYSICAL_PIN\n", program);
    puts("Supported physical pins: 13, 15, 16, 18, 22, 36, 38.");
    puts("Example: sudo ./gpio_output 13");
    puts("One pin at a time. Initial output is LOW (0).");
}

static void print_setting(const struct pin_mapping *pin, const char *chip,
                          int value)
{
    printf("Pin %u | %s | %s | %s offset=%u | OUTPUT=%s (%d) | HOLDING\n",
           pin->physical, pin->purpose, pin->gpio, chip, pin->offset,
           value ? "HIGH" : "LOW", value);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(argv[0]);
        return EXIT_SUCCESS;
    }
    if (argc != 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    char *end;
    errno = 0;
    unsigned long physical = strtoul(argv[1], &end, 10);
    if (errno || end == argv[1] || *end != '\0') {
        fputs("Invalid physical pin number.\n", stderr);
        return EXIT_FAILURE;
    }
    const struct pin_mapping *pin = NULL;
    for (size_t i = 0; i < sizeof(mappings) / sizeof(mappings[0]); ++i) {
        if (physical == mappings[i].physical) {
            pin = &mappings[i];
            break;
        }
    }
    if (!pin) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    struct sigaction action = {0};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) < 0 ||
        sigaction(SIGTERM, &action, NULL) < 0 ||
        sigaction(SIGHUP, &action, NULL) < 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    char chip_path[32];
    char expected_label[16];
    snprintf(chip_path, sizeof(chip_path), "/dev/gpiochip%u", pin->bank);
    snprintf(expected_label, sizeof(expected_label), "gpio%u", pin->bank);
    struct gpiod_chip *chip = gpiod_chip_open(chip_path);
    if (!chip) {
        perror(chip_path);
        return EXIT_FAILURE;
    }
    const char *label = gpiod_chip_label(chip);
    if (!label || strcmp(label, expected_label) != 0) {
        fprintf(stderr, "%s label is '%s', expected '%s'; refusing output.\n",
                chip_path, label ? label : "(none)", expected_label);
        gpiod_chip_close(chip);
        return EXIT_FAILURE;
    }

    struct gpiod_line *line = gpiod_chip_get_line(chip, pin->offset);
    if (!line) {
        perror("gpiod_chip_get_line");
        gpiod_chip_close(chip);
        return EXIT_FAILURE;
    }
    /* No active-low inversion and no open-drain flag: request push-pull output.
     * Set the initial value as part of the request, rather than later.
     */
    if (gpiod_line_request_output(line, "gpio-output-test", 0) < 0) {
        perror("Request output (check line ownership and pinmux)");
        gpiod_chip_close(chip);
        return EXIT_FAILURE;
    }

    printf("libgpiod %s | controller label=%s\n", gpiod_version_string(), label);
    puts("Commands: 0 + Enter = LOW; 1 + Enter = HIGH; q + Enter = exit.");
    puts("Settings are NOT measured voltages. Measure the pin against GND.");
    puts("Keep this program running while measuring; output does not blink.");
    print_setting(pin, chip_path, 0);

    int result = EXIT_SUCCESS;
    char input[64];
    while (!interrupted) {
        fputs("gpio> ", stdout);
        fflush(stdout);
        errno = 0;
        if (!fgets(input, sizeof(input), stdin)) {
            if (!interrupted && ferror(stdin)) {
                perror("Read command");
                result = EXIT_FAILURE;
            }
            break;
        }
        if (interrupted)
            break;
        /* Reject overlong input as a whole, without executing its remainder. */
        if (!strchr(input, '\n') && !feof(stdin)) {
            int character;
            while (!interrupted && (character = getchar()) != '\n' &&
                   character != EOF) {
            }
            puts("Command too long. Use 0, 1, or q, then Enter.");
            continue;
        }
        char *command = input;
        while (isspace((unsigned char)*command))
            ++command;
        char *tail = command + strlen(command);
        while (tail > command && isspace((unsigned char)tail[-1]))
            *--tail = '\0';
        if (strcmp(command, "q") == 0)
            break;
        if (strcmp(command, "0") != 0 && strcmp(command, "1") != 0) {
            puts("Use 0, 1, or q, then Enter. Output setting unchanged.");
            continue;
        }
        int value = command[0] - '0';
        if (gpiod_line_set_value(line, value) < 0) {
            perror("Set output");
            result = EXIT_FAILURE;
            break;
        }
        print_setting(pin, chip_path, value);
    }

    /* Best effort on q, EOF, and handled signals. Released state is not held. */
    if (gpiod_line_set_value(line, 0) < 0) {
        perror("Set LOW before release");
        result = EXIT_FAILURE;
    } else {
        printf("Pin %u | %s | OUTPUT=LOW (0) before release.\n",
               pin->physical, pin->gpio);
    }
    gpiod_line_release(line);
    gpiod_chip_close(chip);
    puts("Released. Output state after release is not guaranteed.");
    return interrupted ? 128 + interrupted : result;
}
