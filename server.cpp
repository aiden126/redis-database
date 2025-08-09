#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <arpa/inet.h>     // for htons, htonl, struct in_addr, sockaddr_in
#include <netinet/in.h>    // for sockaddr_in and in_addr
#include <cassert>         // assert
#include <cerrno>          // errno
#include <cstring>         // memcpy
#include <vector>
#include <poll.h>
#include <fcntl.h>         // file control


// struct sockaddr_in {
//     u_int16_t sin_family;                                        // IP version
//     u_int16_t sin_port;
//     struct in_addr sin_addr;                                     // IPv4
// };

// struct in_addr {
//     u_int32_t s_addr;                                            // IPv4 in big-endian
// };

const size_t k_max_msg = 4096;

struct Conn {
    int fd = -1;

    bool want_read = false;
    bool want_write = false;
    bool want_close = false;

    std::vector<uint8_t> incoming;                                  // read buffer
    std::vector<uint8_t> outgoing;                                  // write buffer
};

struct Buffer {                                                     // rather than removing from front of FIFO vector (O(n^2)), we advance a pointer
    uint8_t *buffer_begin;
    uint8_t *buffer_end;
    uint8_t *data_begin;
    uint8_t *data_end;
};

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

static void fd_set_nb(int fd) {                                         // sets listening socket to non blocking
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);             // accept() returns immediately with new connection or EAGAIN
}

static void buf_append(std::vector<uint8_t> &buf, const uint8_t* data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

static void buf_consume(std::vector<uint8_t> &buf, size_t n) {
    buf.erase(buf.begin(), buf.begin() + n);
}

static Conn* handle_accept(int fd) {                                    // creates a nonblocking listening connection waiting for 1st request
    // accept
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);

    int conn_fd = accept(fd, (struct sockaddr*)&client_addr, &addrlen);
    if (conn_fd < 0) {
        return NULL;
    }

    // set the new fd connection to non blocking mode
    fd_set_nb(conn_fd);

    Conn* conn = new Conn();
    conn->fd = conn_fd;
    conn->want_read = true;                                             // reads 1st request
    return conn;
}

static void handle_read(Conn* conn) {                                   // single read per handle_read() call, event loop reads in chunks
    uint8_t buf[64 * 1024];

    ssize_t rv = read(conn->fd, buf, sizeof(buf));
    if (rv <= 0) {                                                      // handle IO Error (rv < 0) and EOF (rv == 0)                  
        conn->want_close = true;
        return;
    }

    buf_append(conn->incoming, buf, rv);

    while (try_one_request(conn)) {}                                    // treat input buffer as byte stream (multiple pipelined requests)

    // update readiness intention
    if (conn->outgoing.size() > 0) {
        conn->want_read = false;
        conn->want_write = true;

        return handle_write(conn);                                      // optimization for request-response protocol
    }                                                                   // assume client is ready to be written to because it has sent a request
}                                                                       // thus server can write a response without waiting for event loop

static void handle_write(Conn* conn) {
    assert(conn->outgoing.size() > 0);

    ssize_t rv = write(conn->fd, conn->outgoing.data(), conn->outgoing.size());
    if (rv < 0) {
        if (errno == EAGAIN)                                            // if client is not reading, send buffer (kernel buffer) fills up
            return;

        conn->want_close = true;
        return;
    }

    // remove written data from buffer
    buf_consume(conn->outgoing, (size_t)rv);

    if (conn->incoming.size() == 0) {                                   // either read or write
        conn->want_read = true;
        conn->want_write = false;
    }
}

static bool try_one_request(Conn* conn) {                               // if not enough data, do nothing and wait for future iteration
    if (conn->incoming.size() < 4) {
        return false;
    }

    uint32_t len = 0;                                                   // read message header (len)
    memcpy(&len, conn->incoming.data(), 4);
    if (len > k_max_msg) {                                              // protocol error, message body too long       
        conn->want_close = true;
        return false;
    }

    if (4 + len > conn->incoming.size()) {                              // check if full message has been received yet
        return false;                                                   // + 4 bytes for len header
    }

    const uint8_t* request = &conn->incoming[4];

    // process the parsed message 
    printf("client says: len:%d data:%.*s\n", len, len < 100 ? len : 100, request);

    // generate response
    buf_append(conn->outgoing, (const uint8_t*)&len, 4);
    buf_append(conn->outgoing, request, len);

    // clear incoming buffer
    buf_consume(conn->incoming, len + 4);                               // don't empty buffer because of pipelining (more requests in buffer)
    return true;                                                        // multiple messages can arrive back-to-back in a single read operation
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

    std::vector<Conn*> fd2conn;                                         // map of fds to client connections
    std::vector<struct pollfd> poll_args;

    while (true) {                                                      // event loop
        poll_args.clear();

        struct pollfd pfd = {                                           // listening socket
            fd,
            POLLIN,                                                     // can read
            0
        };
        poll_args.push_back(pfd);
        
        // update poll() args for existing connections
        for (Conn *conn : fd2conn) {                                    // rest are connection sockets
            if (!conn) continue;

            struct pollfd pfd = {
                conn->fd,
                POLLERR,                                                // notify us of socket error
                0
            };

            // set poll() flags
            if (conn->want_read) {
                pfd.events |= POLLIN;                                   // set connection's poll event arg to want read
            }
            if (conn->want_write) {
                pfd.events |= POLLOUT;                                  // want write
            }
            poll_args.push_back(pfd);
        }

        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1);  // returns a list of fds that is ready for IO (blocking)
        if (rv < 0 && errno == EINTR) {                                  // std::vector.data returns pointer to vector
            continue;                                                   // EINTR : if received sys interrupt while waiting for a ready fds
        }
        if (rv < 0) {
            printf("poll() error\n");
            return 1;
        }

        // handle listening socket (server)
        if (poll_args[0].revents) {
            Conn* conn = handle_accept(fd);
            if (conn) {
                if (fd2conn.size() <= (size_t)conn->fd) {
                    fd2conn.resize(conn->fd + 1);
                }
                fd2conn[conn->fd] = conn;
            }
        }

        // handle client connections
        for (size_t i = 1; i < poll_args.size(); i++) {
            uint32_t ready = poll_args[i].revents;                      // retrieve poll() return
            Conn* conn = fd2conn[poll_args[i].fd];

            if (ready & POLLIN) {
                handle_read(conn);
            }
            if (ready & POLLOUT) {
                handle_write(conn);
            }

            if ((ready & POLLERR) || conn->want_close) {                // terminate socket on error or end
                (void)close(conn->fd);                                  // void cast to ignore rv of close()
                fd2conn[conn->fd] = NULL;                               // handle_err() callback 
                delete conn;
            }
        }
    }

    return 0;
}