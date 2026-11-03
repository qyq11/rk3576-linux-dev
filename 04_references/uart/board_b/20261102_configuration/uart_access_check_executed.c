/*
 * 核对五组串口能否打开，并读取当前参数；不向串口写入或读取业务数据。
 * 打开/关闭串口可能改变控制信号，因此在已授权配置后执行，不用于最初的只读盘点。
 * 结果只能证明设备入口和访问权限可用，不代表接线、回环或外设通信通过。
 */
#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/serial.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static unsigned long baud_value(speed_t value)
{
    switch (value) {
    case B0: return 0;
    case B1200: return 1200;
    case B2400: return 2400;
    case B4800: return 4800;
    case B9600: return 9600;
    case B19200: return 19200;
    case B38400: return 38400;
    case B57600: return 57600;
    case B115200: return 115200;
    case B230400: return 230400;
    case B460800: return 460800;
    case B921600: return 921600;
    case B1000000: return 1000000;
    case B1500000: return 1500000;
    case B2000000: return 2000000;
    case B3000000: return 3000000;
    case B4000000: return 4000000;
    default: return (unsigned long)-1;
    }
}

static int inspect_port(const char *path)
{
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "PORT=%s OPEN=FAIL errno=%d %s\n", path, errno, strerror(errno));
        return 1;
    }

    struct termios attributes;
    if (tcgetattr(fd, &attributes) != 0) {
        fprintf(stderr, "PORT=%s ATTRIBUTES=FAIL errno=%d %s\n", path, errno, strerror(errno));
        close(fd);
        return 1;
    }

    unsigned bits = 0;
    switch (attributes.c_cflag & CSIZE) {
    case CS5: bits = 5; break;
    case CS6: bits = 6; break;
    case CS7: bits = 7; break;
    case CS8: bits = 8; break;
    }
    const char *parity = (attributes.c_cflag & PARENB)
        ? ((attributes.c_cflag & PARODD) ? "odd" : "even") : "none";

    printf("PORT=%s OPEN=OK BAUD_IN=%lu BAUD_OUT=%lu DATA_BITS=%u PARITY=%s STOP_BITS=%u RTS_CTS=%s ",
           path, baud_value(cfgetispeed(&attributes)), baud_value(cfgetospeed(&attributes)),
           bits, parity, (attributes.c_cflag & CSTOPB) ? 2U : 1U,
           (attributes.c_cflag & CRTSCTS) ? "on" : "off");

    struct serial_rs485 rs485 = {0};
    if (ioctl(fd, TIOCGRS485, &rs485) == 0) {
        printf("RS485_FLAGS=0x%08x", (unsigned)rs485.flags);
    } else {
        printf("RS485_QUERY_ERRNO=%d", errno);
    }
    printf(" SERIAL_BYTES_WRITTEN=0 SERIAL_BYTES_READ=0\n");

    if (close(fd) != 0) {
        fprintf(stderr, "PORT=%s CLOSE=FAIL errno=%d %s\n", path, errno, strerror(errno));
        return 1;
    }
    return 0;
}

int main(void)
{
    const char *ports[] = {"/dev/ttyS3", "/dev/ttyS4", "/dev/ttyS7", "/dev/ttyS8", "/dev/ttyS9"};
    unsigned failed = 0;
    printf("CHECK=uart_access UID=%lu EUID=%lu SERIAL_DATA_IO=none\n",
           (unsigned long)getuid(), (unsigned long)geteuid());
    for (unsigned index = 0; index < sizeof(ports) / sizeof(ports[0]); ++index)
        failed += (unsigned)inspect_port(ports[index]);
    printf("PORTS=5 PASSED=%u FAILED=%u\n", 5U - failed, failed);
    return failed ? 1 : 0;
}
