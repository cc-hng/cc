#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PROTOCOL 0xa001
#define BUF_SIZE 4096

struct __attribute__((packed)) PacketHeader {
    unsigned int id;
    unsigned int type;
};

enum { TYPE_CONFIG = 1, TYPE_DATA = 2 };

static int tcp_fd = -1;
static pthread_t tcp_thread;
static pthread_mutex_t tcp_mutex = PTHREAD_MUTEX_INITIALIZER;
static int data_pipe[2];

typedef void (*cb_t)(int type, const char *data);
static cb_t user_cb = NULL;

// ---- config parsing ----
// format: port:session:mode:local_port:dest_ip:dest_port:connect_mode:timeout:status
static void parse_config(const char *data, char *ip, int ip_len, int *port) {
    int field = 0;
    while (*data) {
        if (*data == ':') {
            if (++field == 4) break;
        }
        data++;
    }
    if (*data != ':') return;
    data++;
    const char *start = data;
    while (*data && *data != ':') data++;
    int sz = (data - start) < ip_len ? (data - start) : (ip_len - 1);
    memcpy(ip, start, sz);
    ip[sz] = '\0';
    if (*data == ':') *port = atoi(data + 1);
}

// ---- TCP sender thread ----
static void *tcp_sender(void *arg) {
    char ip[64] = {0};
    int port = 0;
    parse_config((const char *)arg, ip, sizeof(ip), &port);
    free(arg);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("tcp socket"); return NULL; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        fprintf(stderr, "[TCP] invalid ip: %s\n", ip);
        close(fd);
        return NULL;
    }

    printf("[TCP] connecting %s:%d...\n", ip, port);
    fflush(stdout);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("tcp connect");
        close(fd);
        return NULL;
    }
    printf("[TCP] connected %s:%d (fd=%d)\n", ip, port, fd);

    pthread_mutex_lock(&tcp_mutex);
    if (tcp_fd >= 0) close(tcp_fd);
    tcp_fd = fd;
    pthread_mutex_unlock(&tcp_mutex);

    char buf[BUF_SIZE];
    while (1) {
        int n = read(data_pipe[0], buf, sizeof(buf));
        if (n <= 0) break;

        pthread_mutex_lock(&tcp_mutex);
        int fd2 = tcp_fd;
        pthread_mutex_unlock(&tcp_mutex);

        int off = 0;
        while (off < n) {
            int w = write(fd2, buf + off, n - off);
            if (w <= 0) { perror("tcp write"); goto done; }
            off += w;
        }
        printf("[TCP] forwarded %d bytes\n", n);
        fflush(stdout);
    }

done:
    pthread_mutex_lock(&tcp_mutex);
    if (tcp_fd >= 0) { close(tcp_fd); tcp_fd = -1; }
    pthread_mutex_unlock(&tcp_mutex);
    return NULL;
}

// ---- callbacks ----
static void on_config(const char *data) {
    // kill old tcp thread
    pthread_mutex_lock(&tcp_mutex);
    if (tcp_fd >= 0) {
        close(tcp_fd);
        tcp_fd = -1;
    }
    pthread_mutex_unlock(&tcp_mutex);

    // restart pipe (wake up old reader)
    close(data_pipe[0]);
    close(data_pipe[1]);
    pipe(data_pipe);

    // start new tcp thread (detached)
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    char *cfg = strdup(data);
    if (pthread_create(&tcp_thread, &attr, tcp_sender, cfg) != 0) {
        perror("pthread_create");
        free(cfg);
    }
    pthread_attr_destroy(&attr);
}

static void on_data(const char *data) {
    // format: port:payload
    const char *payload = strchr(data, ':');
    if (!payload) return;
    payload++;
    write(data_pipe[1], payload, strlen(payload));
}

static void dispatch(int type, const char *data) {
    if (user_cb) user_cb(type, data);
    if (type == TYPE_CONFIG) on_config(data);
    else if (type == TYPE_DATA) on_data(data);
}

int main(int argc, char *argv[]) {
    const char *ifname = "eth0";
    if (argc > 1) ifname = argv[1];

    int sock_fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock_fd < 0) { perror("socket"); return -1; }

    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sll));
    sll.sll_family = PF_PACKET;
    sll.sll_ifindex = if_nametoindex(ifname);
    sll.sll_protocol = htons(ETH_P_ALL);

    if (bind(sock_fd, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
        perror("bind"); close(sock_fd); return -1;
    }

    pipe(data_pipe);

    printf("[Bridge] listening on %s, proto=0x%04x\n", ifname, PROTOCOL);

    while (1) {
        unsigned char frame[1500];
        struct sockaddr_ll from;
        socklen_t from_len = sizeof(from);

        int ret = recvfrom(sock_fd, frame, sizeof(frame), 0,
                           (struct sockaddr *)&from, &from_len);
        if (ret < 0) { perror("recvfrom"); continue; }

        unsigned short ether_type = (frame[12] << 8) | frame[13];
        if (ether_type != PROTOCOL) continue;

        struct PacketHeader *hdr = (struct PacketHeader *)(frame + 14);
        const char *raw = (const char *)(frame + 14 + sizeof(struct PacketHeader));
        int raw_len = ret - 14 - (int)sizeof(struct PacketHeader);
        if (raw_len <= 0) continue;

        char *copy = malloc(raw_len + 1);
        memcpy(copy, raw, raw_len);
        copy[raw_len] = '\0';

        printf("[L2] type=%u id=%u data=%s\n", hdr->type, hdr->id, copy);
        dispatch((int)hdr->type, copy);
        free(copy);
    }

    close(sock_fd);
    return 0;
}
