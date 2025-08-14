#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <cassert>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <fcntl.h>

#include <vector>
#include <string>
#include <map>

const uint32_t k_max_msg = 4096;

static int32_t read_full(int fd, char* buf, size_t n) {
    while (n > 0) {
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0) {
            return -1;
        }

        assert((size_t)rv <= n);

        n -= (size_t)rv;
        buf += rv;
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

static int32_t query(int fd, const char* text) {
    uint32_t len = (uint32_t)strlen(text);
    if (len > k_max_msg) {
        printf("query too long");
        return -1;
    }

    // send request
    char wbuf[4 + k_max_msg];
    memcpy(wbuf, &len, 4);
    memcpy(wbuf + 4, text, len);

    uint32_t err = write_all(fd, wbuf, 4 + len);
    if (err) {
        printf("write() error\n");
        return err;
    }

    // read response
    char rbuf[4 + k_max_msg + 1];                                       // allocate extra for defensive coding (null terminator)
    errno = 0;

    err = read_full(fd, rbuf, 4);
    if (err) {
        printf("read() error");
        return err;
    }

    memcpy(&len, rbuf, 4);
    if (len > k_max_msg) {
        printf("response too long");
        return -1;
    }

    err = read_full(fd, rbuf + 4, len);
    if (err) {
        printf("read() error");
        return err;
    }

    printf("server says: %.*s\n", len, rbuf + 4);

    return 0;
}

int main(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("socket error");
        return 1;
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);                      // 127.0.0.1

    int rv = connect(fd, (const sockaddr*)&addr, sizeof(addr));
    if (rv < 0) {
        printf("connect error");
        return 1;
    }

    std::vector<std::string> query_list = {
        "hello1", "hello2", "hello3"
    };

    for (const std::string &q : query_list) {
        int32_t err = query(fd, q.c_str());
        if (err) {
            printf("error sending query %s\n", q.c_str());
        }
    }

    close(fd);

    return 0;
}