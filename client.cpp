#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>     // for htons, htonl, struct in_addr, sockaddr_in
#include <netinet/in.h>    // for sockaddr_in and in_addr


// struct sockaddr_in {
//     u_int16_t sin_family;       // IP version
//     u_int16_t sin_port;
//     struct addr_in sin_addr;    // IPv4
// };

// struct addr_in {
//     u_int32_t s_addr;           // IPv4 in big-endian
// };

int main(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("socket error");
        return 1;
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);      // 127.0.0.1

    int rv = connect(fd, (const sockaddr*)&addr, sizeof(addr));
    if (rv < 0) {
        printf("connect error");
        return 1;
    }

    char msg[] = "hello";
    write(fd, msg, sizeof(msg));

    char rbuf[64] = {};
    ssize_t n = read(fd, rbuf, sizeof(rbuf) - 1);
    if (n < 0) {
        printf("read error");
        return 1;
    }

    printf("server says: %s\n", rbuf);
    close(fd);

    return 0;
}