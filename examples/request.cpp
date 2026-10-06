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

#define PROTOCOL 0xa001
#define MAX_RETRY 3
#define TIMEOUT_MS 2000

struct __attribute__((packed)) packet_header_t {
    unsigned int id;
    unsigned int type;
};

enum { TYPE_REQUEST = 1, TYPE_RESPONSE = 2 };

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: %s <dst_mac> <message>\n", argv[0]);
        printf("Example: %s aa:bb:cc:dd:ee:ff Hello\n", argv[0]);
        return 0;
    }

    int sock_fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock_fd < 0) {
        perror("socket failed");
        return -1;
    }

    struct sockaddr_ll addr;
    memset(&addr, 0, sizeof(addr));
    addr.sll_family = PF_PACKET;
    addr.sll_ifindex = if_nametoindex("eth0");
    addr.sll_protocol = htons(ETH_P_ALL);

    if (bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        close(sock_fd);
        return -1;
    }

    unsigned char dst_mac[6];
    unsigned int mac[6];
    if (sscanf(argv[1], "%x:%x:%x:%x:%x:%x", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) != 6) {
        fprintf(stderr, "Invalid MAC address format\n");
        close(sock_fd);
        return -1;
    }
    for (int i = 0; i < 6; ++i) {
        dst_mac[i] = (unsigned char)mac[i];
    }

    unsigned char src_mac[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    const char* msg = argv[2];

    unsigned int req_id = (unsigned int)(getpid() & 0xFFFFFFFF);

    unsigned char frame[1500];
    memcpy(frame, dst_mac, 6);
    memcpy(frame + 6, src_mac, 6);
    memcpy(frame + 12, "\xa0\x01", 2);

    struct packet_header_t* hdr = (struct packet_header_t*)(frame + 14);
    hdr->id = req_id;
    hdr->type = TYPE_REQUEST;
    memcpy(frame + 14 + sizeof(struct packet_header_t), msg, strlen(msg));

    int payload_len = sizeof(struct packet_header_t) + strlen(msg);
    int total_len = 14 + payload_len;

    for (int retry = 0; retry < MAX_RETRY; ++retry) {
        printf("[Request] ID=%u retry=%d send: %s\n", req_id, retry, msg);
        fflush(stdout);

        int ret = sendto(sock_fd, frame, total_len, 0, (struct sockaddr*)&addr, sizeof(addr));
        if (ret < 0) {
            perror("sendto failed");
            break;
        }

        struct timeval tv;
        tv.tv_sec = TIMEOUT_MS / 1000;
        tv.tv_usec = (TIMEOUT_MS % 1000) * 1000;
        setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        unsigned char recv_buf[1500];
        struct sockaddr_ll from;
        socklen_t from_len = sizeof(from);

        while (1) {
            ret = recvfrom(sock_fd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr*)&from, &from_len);
            if (ret < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    printf("[Request] timeout, retrying...\n");
                    fflush(stdout);
                } else {
                    perror("recvfrom failed");
                }
                break;
            }

            unsigned short ether_type = (recv_buf[12] << 8) | recv_buf[13];
            if (ether_type != PROTOCOL) {
                continue;
            }

            if (memcmp(recv_buf + 6, src_mac, 6) == 0) {
                continue;
            }

            struct packet_header_t* recv_hdr = (struct packet_header_t*)(recv_buf + 14);
            printf("[Request] got frame: type=%u id=%u (expect id=%u)\n", recv_hdr->type, recv_hdr->id, req_id);
            fflush(stdout);

            if (recv_hdr->type != TYPE_RESPONSE) {
                continue;
            }
            if (recv_hdr->id != req_id) {
                continue;
            }

            const char* resp = (const char*)(recv_buf + 14 + sizeof(struct packet_header_t));
            int resp_len = ret - 14 - sizeof(struct packet_header_t);
            printf("[Response] ID=%u data: %.*s\n", recv_hdr->id, resp_len, resp);
            fflush(stdout);

            close(sock_fd);
            return 0;
        }
    }

    printf("[Request] Failed after %d retries\n", MAX_RETRY);
    close(sock_fd);
    return -1;
}
