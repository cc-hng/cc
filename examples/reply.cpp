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

constexpr unsigned short PROTOCOL = 0xa001;
constexpr int RECV_TIMEOUT_MS = 5000;

struct __attribute__((packed)) packet_header_t {
    unsigned int id;
    unsigned int type;
};

enum : unsigned int {
    TYPE_REQUEST = 1,
    TYPE_RESPONSE = 2,
};

int main() {
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

    unsigned char src_mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    struct timeval tv;
    tv.tv_sec = RECV_TIMEOUT_MS / 1000;
    tv.tv_usec = (RECV_TIMEOUT_MS % 1000) * 1000;
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    printf("Waiting for requests...\n");
    fflush(stdout);

    while (1) {
        unsigned char frame[1500];
        struct sockaddr_ll from;
        socklen_t from_len = sizeof(from);
        memset(&from, 0, sizeof(from));

        int ret = recvfrom(sock_fd, frame, sizeof(frame), 0, (struct sockaddr*)&from, &from_len);
        if (ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("[Timeout] no request received in %d ms\n", RECV_TIMEOUT_MS);
            } else {
                perror("recvfrom failed");
            }
            fflush(stdout);
            continue;
        }

        unsigned short ether_type = (frame[12] << 8) | frame[13];
        if (ether_type != PROTOCOL) {
            continue;
        }

        packet_header_t* hdr = reinterpret_cast<packet_header_t*>(frame + 14);
        if (hdr->type != TYPE_REQUEST) {
            continue;
        }

        printf("\n[Request] ID=%u from=%02x:%02x:%02x:%02x:%02x:%02x\n", hdr->id, from.sll_addr[0],
               from.sll_addr[1], from.sll_addr[2], from.sll_addr[3], from.sll_addr[4], from.sll_addr[5]);

        const char* req_data = reinterpret_cast<const char*>(frame + 14 + sizeof(packet_header_t));
        int req_len = ret - 14 - sizeof(packet_header_t);
        printf("  data: %.*s\n", req_len, req_data);
        fflush(stdout);

        char resp_data[256];
        int resp_len = snprintf(resp_data, sizeof(resp_data), "Reply_%u_%.*s", hdr->id, req_len, req_data);

        unsigned char resp_frame[1500];
        memcpy(resp_frame, frame + 6, 6);
        memcpy(resp_frame + 6, src_mac, 6);
        memcpy(resp_frame + 12, "\xa0\x01", 2);

        packet_header_t* resp_hdr = reinterpret_cast<packet_header_t*>(resp_frame + 14);
        resp_hdr->id = hdr->id;
        resp_hdr->type = TYPE_RESPONSE;
        memcpy(resp_frame + 14 + sizeof(packet_header_t), resp_data, resp_len);

        int total_len = 14 + sizeof(packet_header_t) + resp_len;

        struct sockaddr_ll dest_addr;
        memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.sll_family = PF_PACKET;
        dest_addr.sll_ifindex = if_nametoindex("eth0");
        memcpy(dest_addr.sll_addr, frame + 6, 6);
        dest_addr.sll_halen = 6;

        int send_ret =
            sendto(sock_fd, resp_frame, total_len, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        if (send_ret > 0) {
            printf("[Response] ID=%u sent: %s\n", hdr->id, resp_data);
            fflush(stdout);
        }
    }

    close(sock_fd);
    return 0;
}