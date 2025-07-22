#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <arpa/inet.h>     // for htons, htonl, struct in_addr, sockaddr_in
#include <netinet/in.h>    // for sockaddr_in and in_addr
#include <cassert>         // assert
#include <cerrno>          // errno
#include <cstring>         // memcpy


// struct sockaddr_in {
//     u_int16_t sin_family;                                        // IP version
//     u_int16_t sin_port;
//     struct in_addr sin_addr;                                     // IPv4
// };

// struct in_addr {
//     u_int32_t s_addr;                                            // IPv4 in big-endian
// };

const size_t k_max_msg = 4096;

static int32_t read_full(int fd, char* buf, size_t n) {             // restrict to current file with static
    while (n > 0) {
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0) {
            //perror("read error");
            return -1;                                              // error or unexpected EOF
        }

        assert((size_t)rv <= n);                                    // sanity check

        n -= (size_t)rv;
        buf += rv;                                                  // move buffer pointer by read bytes
    }

    return 0;
}

static int32_t write_all(int fd, char* buf, size_t n) {
    while (n > 0) {
        ssize_t rv = write(fd, buf, n);
        if (rv <= 0) {
            return -1;                                              
        }

        assert((size_t)rv <= n);

        n -= (size_t)rv;
        buf += rv;
    }

    return 0;
}

static int32_t one_request(int conn_fd) {
    char rbuf[4 + k_max_msg];                                           // 4 byte length header
    errno = 0;

    int32_t err = read_full(conn_fd, rbuf, 4);
    if (err) {
        if (errno != 0)
            printf("header read() error\n");
        return err;
    }

    uint32_t len = 0;                                                   // read length header
    memcpy(&len, rbuf, 4);
    if (len > k_max_msg) {
        printf("message too long\n");
        return -1;
    }

    err = read_full(conn_fd, rbuf + 4, len);                            // read message body
    if (err) {
        printf("body read() error");
        return err;
    }

    printf("client says %.*s\n", len, rbuf + 4);                        // %.*s : len, string    
    
    char reply[] = "world";
    char wbuf[4 + sizeof(reply)];                                       // use sizeof to allocate bytes for null terminator

    len = (uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);                                              // save len header
    memcpy(wbuf + 4, reply, len);                                       // save body

    return write_all(conn_fd, wbuf, 4 + len);                                // do not send null terminator over protocols
}

int main(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);                           // obtain socket handle

    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));        // set socket option to enable reuse of same socket (ip:port)


    struct sockaddr_in addr = {};                                       // bind socket to address
    addr.sin_family = AF_INET;                                          // IPv4
    addr.sin_port = htons(1234);                                        // host to network short port 1234
    addr.sin_addr.s_addr = htonl(0);                                    // wildcard IP 0.0.0.0
    
    int rv = bind(fd, (const struct sockaddr*)&addr, sizeof(addr));
    if (rv) {
        perror("bind failed");
        return 1;
    }

    rv = listen(fd, SOMAXCONN);                                         // create the socket
    if (rv) {
        printf("listen()");
        return 1;
    }

    while (true) {                                                      // accept connections
        struct sockaddr_in client_addr = {};
        socklen_t addr_len = sizeof(client_addr);

        int conn_fd = accept(fd, (struct sockaddr*)&client_addr, &addr_len);
        if (conn_fd < 0) {
            continue;
        }

        while (true) {
            int32_t err = one_request(conn_fd);                          // keep connection open and allow multiple requests
            if (err) {
                break;
            }
        }

        close(conn_fd);
    }

    return 0;
}