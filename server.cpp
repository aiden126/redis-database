#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <arpa/inet.h>     // for htons, htonl, struct in_addr, sockaddr_in
#include <netinet/in.h>    // for sockaddr_in and in_addr


// struct sockaddr_in {
//     u_int16_t sin_family;       // IP version
//     u_int16_t sin_port;
//     struct in_addr sin_addr;    // IPv4
// };

// struct in_addr {
//     u_int32_t s_addr;           // IPv4 in big-endian
// };

static void do_something(int conn_fd) {
    char rbuf[64] = {};
    ssize_t n = read(conn_fd, rbuf, sizeof(rbuf) - 1);
    if (n < 0) {
        printf("read() error");
        return;
    }

    printf("client says: %s\n", rbuf);

    char wbuf[] = "world";
    write(conn_fd, wbuf, sizeof(wbuf));
}

int main(void) {
    // obtain socket handle
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    // set socket option to enable reuse of same socket (ip:port)
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));


    // bind socket to address
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;          // IPv4
    addr.sin_port = htons(1234);        // host to network short port 1234
    addr.sin_addr.s_addr = htonl(0);    // wildcard IP 0.0.0.0
    
    int rv = bind(fd, (const struct sockaddr*)&addr, sizeof(addr));
    if (rv) {
        printf("bind()");
        return 1;
    }

    // create the socket
    rv = listen(fd, SOMAXCONN);
    if (rv) {
        printf("listen()");
        return 1;
    }

    // accept connections
    while (true) {
        struct sockaddr_in client_addr = {};
        socklen_t addr_len = sizeof(client_addr);

        int conn_fd = accept(fd, (struct sockaddr*)&client_addr, &addr_len);
        if (conn_fd < 0) {
            continue;
        }

        do_something(conn_fd);
        close(conn_fd);
    }

    return 0;
}