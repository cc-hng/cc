#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main() {
    int sock_fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock_fd < 0) {
        perror("socket failed");
        return -1;
    }

    int ifindex = if_nametoindex("eth0");
    printf("ifindex = %d\n", ifindex);

    struct sockaddr_ll addr;
    memset(&addr, 0, sizeof(addr));
    addr.sll_family = PF_PACKET;
    addr.sll_ifindex = ifindex;
    addr.sll_protocol = htons(ETH_P_ALL);

    if (bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        close(sock_fd);
        return -1;
    }

    unsigned char frame[1500];

    printf("等待接收...\n");
    fflush(stdout);

    while (1) {
        struct sockaddr_ll from;
        socklen_t from_len = sizeof(from);
        memset(&from, 0, sizeof(from));

        int ret = recvfrom(sock_fd, frame, sizeof(frame), 0,
                           (struct sockaddr *)&from, &from_len);
        if (ret < 0) {
            perror("recvfrom failed");
            continue;
        }

        unsigned short ether_type = (frame[12] << 8) | frame[13];
        printf("\nrecv: ret=%d, ifindex=%d, ether_type=0x%04x\n",
               ret, from.sll_ifindex, ether_type);
        printf("  dst_mac: %02x:%02x:%02x:%02x:%02x:%02x\n",
               frame[0], frame[1], frame[2], frame[3], frame[4], frame[5]);
        printf("  src_mac: %02x:%02x:%02x:%02x:%02x:%02x\n",
               frame[6], frame[7], frame[8], frame[9], frame[10], frame[11]);

        if (ether_type == 0xa001) {
            printf("  [匹配!] 数据: %.*s\n", ret - 14, frame + 14);
        }
        fflush(stdout);
    }

    close(sock_fd);
    return 0;
}