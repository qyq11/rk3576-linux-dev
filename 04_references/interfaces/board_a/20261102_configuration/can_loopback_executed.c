#include <errno.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

/* 只在已断开外部设备、并用ip命令启用控制器内部回环后运行。
 * 默认检查发送后由驱动回送的确认，打印完整内容和消息来源。
 * --no-local-echo关闭发送确认回送，单独检查接收通道是否返回数据。
 * 两种结果分开记录，不能据此证明外部接线正常。
 */
static void print_frame(const char *direction, size_t sequence,
                        const struct can_frame *frame)
{
    const unsigned int mask = (frame->can_id & CAN_EFF_FLAG)
                              ? CAN_EFF_MASK : CAN_SFF_MASK;
    printf("%s seq=%zu %s ID=%0*X DLC=%u DATA=", direction, sequence,
           (frame->can_id & CAN_EFF_FLAG) ? "extended" : "standard",
           (frame->can_id & CAN_EFF_FLAG) ? 8 : 3, frame->can_id & mask,
           frame->can_dlc);
    for (unsigned int i = 0; i < frame->can_dlc; ++i)
        printf("%s%02X", i ? " " : "", frame->data[i]);
    if (!frame->can_dlc)
        printf("(empty)");
    putchar('\n');
}

int main(int argc, char **argv)
{
    const struct can_frame frames[] = {
        {.can_id = 0x123, .can_dlc = 8,
         .data = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}},
        {.can_id = 0x321, .can_dlc = 0},
        {.can_id = CAN_EFF_FLAG | 0x001abcde, .can_dlc = 8,
         .data = {0x00, 0xff, 0x55, 0xaa, 0x01, 0x02, 0x03, 0x04}}
    };
    struct ifreq request = {0};
    struct sockaddr_can address = {.can_family = AF_CAN};
    int receive_own = 1;
    int local_echo = 1;
    int socket_fd;
    int result = 1;

    setvbuf(stdout, NULL, _IOLBF, 0);
    if ((argc != 2 && argc != 3) || strlen(argv[1]) >= IFNAMSIZ ||
        (argc == 3 && strcmp(argv[2], "--no-local-echo"))) {
        fprintf(stderr, "Usage: %s CAN_INTERFACE [--no-local-echo]\n", argv[0]);
        return 2;
    }
    if (argc == 3)
        local_echo = 0;
    socket_fd = socket(PF_CAN, SOCK_RAW | SOCK_CLOEXEC, CAN_RAW);
    if (socket_fd < 0) {
        perror("socket");
        return 1;
    }
    strcpy(request.ifr_name, argv[1]);
    if (ioctl(socket_fd, SIOCGIFINDEX, &request) < 0) {
        perror("SIOCGIFINDEX");
        goto finish;
    }
    address.can_ifindex = request.ifr_ifindex;
    if (setsockopt(socket_fd, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS,
                   &receive_own, sizeof(receive_own)) < 0 ||
        setsockopt(socket_fd, SOL_CAN_RAW, CAN_RAW_LOOPBACK,
                   &local_echo, sizeof(local_echo)) < 0 ||
        bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("CAN socket setup");
        goto finish;
    }
    printf("interface=%s required-mode=controller-loopback classic-CAN local-echo=%s\n",
           argv[1], local_echo ? "enabled" : "disabled");
    for (size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); ++i) {
        struct can_frame received = {0};
        struct iovec buffer = {.iov_base = &received, .iov_len = sizeof(received)};
        struct msghdr message = {.msg_iov = &buffer, .msg_iovlen = 1};
        struct pollfd wait_fd = {.fd = socket_fd, .events = POLLIN};
        int ready;
        if (write(socket_fd, &frames[i], sizeof(frames[i])) != sizeof(frames[i])) {
            perror("CAN write");
            goto finish;
        }
        print_frame("TX", i + 1, &frames[i]);
        do {
            ready = poll(&wait_fd, 1, 1000);
        } while (ready < 0 && errno == EINTR);
        if (ready <= 0 || !(wait_fd.revents & POLLIN)) {
            fprintf(stderr, "RX seq=%zu failed: timeout or socket error\n", i + 1);
            goto finish;
        }
        if (recvmsg(socket_fd, &message, 0) != sizeof(received)) {
            perror("CAN recvmsg");
            goto finish;
        }
        if (received.can_dlc > CAN_MAX_DLEN) {
            fprintf(stderr, "Invalid CAN data length\n");
            goto finish;
        }
        print_frame("RX", i + 1, &received);
        printf("RX seq=%zu source=%s flags=0x%X\n", i + 1,
               (message.msg_flags & MSG_CONFIRM) ? "tx-confirmation" : "receive-path",
               message.msg_flags);
        if (received.can_id != frames[i].can_id ||
            received.can_dlc != frames[i].can_dlc ||
            memcmp(received.data, frames[i].data, received.can_dlc)) {
            fprintf(stderr, "RX seq=%zu differs from TX\n", i + 1);
            goto finish;
        }
    }
    puts("3 transmitted frames match 3 received frames.");
    result = 0;
finish:
    close(socket_fd);
    return result;
}
