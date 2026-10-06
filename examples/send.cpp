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
    // ====================== 1. 创建原始套接字 ======================
    // 原始报文里面前六个字节是目标mac 6-11是自己的mac 12 和13是自定义协议类型叫0xa001吧
    // 我这边只要收到这个就知道是你发给的我  然后解析后面在字段 后面的内容就按你想发送的随便来
    int sock_fd = socket(PF_PACKET, SOCK_RAW, htons(0xa001));  // 0xa001=自定义协议
    if (sock_fd < 0) {
        perror("socket failed");
        return 0;
    }

    // ====================== 2. 绑定网卡（eth0） ====================
    struct sockaddr_ll addr;
    memset(&addr, 0, sizeof(addr));
    addr.sll_family = PF_PACKET;
    addr.sll_ifindex = if_nametoindex("eth0");  // 你的网卡名
    addr.sll_protocol = htons(0xa001);

    if (bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind failed");

        close(sock_fd);
        return -1;
    }

    // ====================== 3. 构造以太网帧 ========================
    // unsigned char dst_mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};  // 目标MAC
    unsigned char dst_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};  // 目标MAC
    unsigned char src_mac[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};  // 本机MAC
    unsigned char data[64] = "I_AM_RAW_NO_IP_DATA";                   // 原始数据（无格式）

    // 以太网帧 = 14字节头部 + 数据
    unsigned char frame[1500];
    memcpy(frame, dst_mac, 6);
    memcpy(frame + 6, src_mac, 6);
    memcpy(frame + 12, "\xa0\x01", 2);  // 自定义协议
    memcpy(frame + 14, data, strlen((const char *)data));

    int len = 14 + strlen((const char *)data);

    // ====================== 4. 发送（无 IP！直接发二层！） ==========
    int ret = sendto(sock_fd, frame, len, 0, (struct sockaddr *)&addr, sizeof(addr));

    if (ret > 0) {
        printf("发送成功！无IP RAW以太网帧，长度：%d\n", ret);
    }

    close(sock_fd);
    return 0;
}
