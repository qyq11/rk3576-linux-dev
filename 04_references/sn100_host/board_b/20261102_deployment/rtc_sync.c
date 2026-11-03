/* 板B缺少hwclock命令，直接用内核接口把已校正的系统UTC时间写入硬件时钟。 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static void print_rtc(const char *label, const struct rtc_time *value)
{
    printf("%s=%04d-%02d-%02dT%02d:%02d:%02dZ\n", label,
           value->tm_year + 1900, value->tm_mon + 1, value->tm_mday,
           value->tm_hour, value->tm_min, value->tm_sec);
}

int main(void)
{
    char serial[32] = {0};
    int identity = open("/proc/device-tree/serial-number", O_RDONLY);
    if (identity < 0 || read(identity, serial, sizeof(serial) - 1) <= 0)
    {
        perror("read board identity");
        return 1;
    }
    close(identity);
    if (strcmp(serial, "8314b1e8ffd0321f") != 0)
    {
        fprintf(stderr, "Refusing to change a board other than board_b\n");
        return 2;
    }
    int rtc = open("/dev/rtc0", O_RDWR);
    struct rtc_time before, after;
    time_t now = time(NULL);
    struct tm utc;
    if (rtc < 0 || !gmtime_r(&now, &utc) || ioctl(rtc, RTC_RD_TIME, &before) < 0)
    {
        perror("read RTC");
        return 1;
    }
    print_rtc("RTC_BEFORE", &before);
    struct rtc_time target = {
        .tm_sec = utc.tm_sec, .tm_min = utc.tm_min, .tm_hour = utc.tm_hour,
        .tm_mday = utc.tm_mday, .tm_mon = utc.tm_mon, .tm_year = utc.tm_year,
        .tm_wday = utc.tm_wday, .tm_yday = utc.tm_yday, .tm_isdst = 0
    };
    print_rtc("SYSTEM_UTC", &target);
    if (ioctl(rtc, RTC_SET_TIME, &target) < 0 || ioctl(rtc, RTC_RD_TIME, &after) < 0)
    {
        perror("set and verify RTC");
        close(rtc);
        return 1;
    }
    close(rtc);
    print_rtc("RTC_AFTER", &after);
    struct tm check = {
        .tm_sec = after.tm_sec, .tm_min = after.tm_min, .tm_hour = after.tm_hour,
        .tm_mday = after.tm_mday, .tm_mon = after.tm_mon, .tm_year = after.tm_year,
        .tm_isdst = 0
    };
    time_t rtc_epoch = timegm(&check);
    long difference = (long)difftime(rtc_epoch, now);
    printf("RTC_DIFFERENCE_SECONDS=%ld\n", difference);
    if (difference < -2 || difference > 2)
        return 1;
    printf("RTC_WRITE_AND_READBACK_OK=1\nREBOOT_TIME_KEEPING_VERIFIED=0\n");
    return 0;
}
