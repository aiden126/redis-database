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
#include <math.h>
#include <time.h>

#include <vector>
#include <string>
#include <map>

#include "hashtable.h"
#include "zset.h"
#include "common.h"
#include "dlist.h"

const size_t k_max_msg = 4096;

typedef std::vector<uint8_t> Buffer;

struct Conn {
    int fd = -1;

    bool want_read = false;
    bool want_write = false;
    bool want_close = false;

    Buffer incoming;
    Buffer outgoing;

    uint64_t last_active_ms = 0;
    DList idle_node;
};

static struct {
    HMap db;
    std::vector<Conn*> fd2conn; 
    DList idle_list; 
} g_data;

enum {
    T_INIT = 0,
    T_STR  = 1,
    T_ZSET = 2,
};

// KV pair for hashtable
struct Entry {
    struct HNode node;
    std::string key;

    uint32_t type = 0;
    std::string str;
    ZSet zset;
};

static Entry *entry_new(uint32_t type) {
    Entry *ent = new Entry();
    ent->type = type;
    return ent;
}

static void entry_del(Entry *ent) {
    if (ent->type == T_ZSET) {
        zset_clear(&ent->zset);
    }

    delete ent; 
}

static bool entry_eq(HNode *lhs, HNode *rhs) {
    struct Entry *le = container_of(lhs, struct Entry, node);
    struct Entry *re = container_of(rhs, struct Entry, node);

    return le->key == re->key;
}

static void conn_destroy(Conn *conn) {
    close(conn->fd);
    g_data.fd2conn[conn->fd] = NULL;
    dlist_detach(&conn->idle_node);
    delete conn;
}

static void fd_set_nb(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

static void buf_append(std::vector<uint8_t> &buf, const uint8_t* data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

static void buf_consume(std::vector<uint8_t> &buf, size_t n) {
    buf.erase(buf.begin(), buf.begin() + n);
}

enum {
    ERR_UNKNOWN  = 1,
    ERR_TOO_BIG  = 2,
    ERR_BAD_TYPE = 3,
    ERR_BAD_ARG  = 4,
};

enum {
    TAG_NIL = 0,
    TAG_ERR = 1,
    TAG_STR = 2,
    TAG_INT = 3,
    TAG_DBL = 4,
    TAG_ARR = 5,
};

static uint64_t get_monotonic_msec() {
    struct timespec tv = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_nsec / 1000 / 1000;
}

// helper functions for serialization
static void buf_append_u8(Buffer &buf, uint8_t data) {
    buf.push_back(data);
}

static void buf_append_u32(Buffer &buf, uint32_t data) {
    buf_append(buf, (const uint8_t *)&data, 4);
}

static void buf_append_i64(Buffer &buf, int64_t data) {
    buf_append(buf, (const uint8_t *)&data, 8);
}

static void buf_append_dbl(Buffer &buf, double data) {
    buf_append(buf, (const uint8_t *)&data, 8);
}

// append serialized data to back
static void out_nil(Buffer &out) {
    buf_append_u8(out, TAG_NIL);
}

static void out_str(Buffer &out, const char *s, size_t size) {
    buf_append_u8(out, TAG_STR);
    buf_append_u32(out, (uint32_t)size);
    buf_append(out, (const uint8_t *)s, size);
}

static void out_int(Buffer &out, int64_t val) {
    buf_append_u8(out, TAG_INT);
    buf_append_i64(out, val);
}

static void out_dbl(Buffer &out, double val) {
    buf_append_u8(out, TAG_DBL);
    buf_append_dbl(out, val);
}

static void out_arr(Buffer &out, uint32_t n) {
    buf_append_u8(out, TAG_ARR);
    buf_append_u32(out, n);
}

static void out_err(Buffer &out, uint32_t code, const std::string &msg) {
    buf_append_u8(out, TAG_ERR);
    buf_append_u32(out, code);
    buf_append_u32(out, (uint32_t)msg.size());
    buf_append(out, (const uint8_t *)msg.data(), msg.size());
}

static size_t out_begin_arr(Buffer &out) {
    out.push_back(TAG_ARR);
    buf_append_u32(out, 0);
    return out.size() - 4;
}

static void out_end_arr(Buffer &out, size_t ctx, uint32_t n) {
    memcpy(&out[ctx], &n, 4);
}

// read (int) 4 bytes from byte stream
static bool read_u32(const uint8_t *&cur, const uint8_t *end, uint32_t &out) { 
    if (cur + 4 > end) {
        return false;
    }

    memcpy(&out, cur, 4);
    cur += 4;
    return true;
}

// read (string) n bytes from byte stream
static bool read_str(const uint8_t *&cur, const uint8_t *end, size_t n, std::string &out) {
    if (cur + n > end) {
        return false;
    }

    out.assign(cur, cur + n);
    cur += n;
    return true;
}

static int32_t parse_req(const uint8_t *data, size_t size, std::vector<std::string> &out) {
    const uint8_t *end = data + size;
    uint32_t nstr = 0;
    if (!read_u32(data, end, nstr)) {
        printf("error reading nstr\n");
        return -1;
    }

    if (nstr > k_max_msg) {
        printf("nstr too large\n");
        return -1;
    }

    while (out.size() < nstr) {
        uint32_t len = 0;
        if (!read_u32(data, end, len)) {
            printf("error reading request len\n");
            return -1;
        }

        out.push_back(std::string());
        if (!read_str(data, end, len, out.back())) {
            printf("error reading request str\n");
            return -1;
        }
    }

    if (data != end) {
        return -1;
    }

    return 0;
}

static void do_get(std::vector<std::string> &cmd, Buffer &out) {
    struct Entry key;       // dummy entry
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if (!node) {
        return out_nil(out);
    }

    const std::string &val = container_of(node, struct Entry, node)->str;
    return out_str(out, val.data(), val.size());
}

static void do_set(std::vector<std::string> &cmd, Buffer &out) {
    struct Entry key;       // dummy entry
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if (node) {
        struct Entry *target = container_of(node, Entry, node);
        target->str.swap(cmd[2]);
    } else {
        struct Entry *ent = new Entry();
        ent->key.swap(key.key);
        ent->node.hcode = key.node.hcode;
        ent->str.swap(cmd[2]);

        hm_insert(&g_data.db, &ent->node);
    }

    return out_nil(out);
}

static void do_del(std::vector<std::string> &cmd, Buffer &out) {
    struct Entry key;       // dummy entry
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    HNode *node = hm_delete(&g_data.db, &key.node, &entry_eq);
    if (node) {
        entry_del(container_of(node, Entry, node));
    }

    return out_int(out, node ? 1 : 0);
}

static bool cb_keys(HNode *node, void *args) {
    Buffer &out = *(Buffer *)args;
    const std::string &key = container_of(node, Entry, node)->key;

    out_str(out, key.data(), key.size());
    return true;
}

static void do_keys(std::vector<std::string> &, Buffer &out) {
    out_arr(out, (uint32_t)hm_size(&g_data.db));
    hm_foreach(&g_data.db, cb_keys, (void *)&out);
}

static bool str2dbl(const std::string &s, double &out) {
    char *endp = NULL;
    out = strtod(s.c_str(), &endp);
    return endp == s.c_str() + s.size() && !isnan(out);
}

static bool str2int(const std::string &s, int64_t &out) {
    char *endp = NULL;
    out = strtoll(s.c_str(), &endp, 10);
    return endp == s.c_str() + s.size();
}

static void do_zadd(std::vector<std::string> &cmd, Buffer &out) {
    double score = 0;
    if (!str2dbl(cmd[2], score)) {
        return out_err(out, ERR_BAD_ARG, "expected fp value");
    }

    HNode node;
    std::string key;

    key.swap(cmd[1]);
    node.hcode = str_hash((uint8_t *)key.data(), key.size());
    
    HNode *hnode = hm_lookup(&g_data.db, &node, &entry_eq);

    Entry *ent = NULL;
    if (!hnode) {
        ent = entry_new(T_ZSET);
        ent->key.swap(key);
        ent->node.hcode = node.hcode;
        hm_insert(&g_data.db, &ent->node);
    } else {
        ent = container_of(hnode, Entry, node);
        if (ent->type != T_ZSET) {
            return out_err(out, ERR_BAD_TYPE, "expected zset");
        }
    }

    const std::string &name = cmd[3];
    bool added = zset_insert(&ent->zset, name.data(), name.size(), score);
    return out_int(out, (uint64_t)added);
}

static ZSet *expect_zset(std::string &s) {
    struct HNode node;
    std::string key;

    key.swap(s);
    node.hcode = str_hash((uint8_t *)key.data(), key.size());

    HNode *hnode = hm_lookup(&g_data.db, &node, &entry_eq);
    if (!hnode) {
        return NULL;
    }

    Entry *ent = container_of(hnode, Entry, node);
    return ent->type == T_ZSET ? &ent->zset : NULL;
}

static void do_zrem(std::vector<std::string> &cmd, Buffer &out) {
    ZSet *zset = expect_zset(cmd[1]);
    if (!zset) {
        return out_err(out, ERR_BAD_TYPE, "expected zset");
    }

    const std::string &name = cmd[2];
    ZNode *znode = zset_lookup(zset, name.data(), name.size());
    if (znode) {
        zset_delete(zset, znode);
    }

    return out_int(out, znode ? 1 : 0);
}

static void do_zscore(std::vector<std::string> &cmd, Buffer &out) {
    ZSet *zset = expect_zset(cmd[1]);
    if (!zset) {
        return out_err(out, ERR_BAD_TYPE, "expected zset");
    }

    const std::string &name = cmd[2];
    ZNode *znode = zset_lookup(zset, name.data(), name.size());

    if (znode) {
        out_dbl(out, znode->score);
    } else {
        out_nil(out);
    }
}

static void do_zquery(std::vector<std::string> &cmd, Buffer &out) {
    // parse arguments
    double score = 0;
    if (!str2dbl(cmd[2], score)) {
        return out_err(out, ERR_BAD_ARG, "expected fp number");
    }

    const std::string &name = cmd[3];
    int64_t offset = 0;
    int64_t limit = 0;
    if (!str2int(cmd[4], offset) || !str2int(cmd[5], limit)) {
        return out_err(out, ERR_BAD_ARG, "expected int");
    }

    ZSet *zset = expect_zset(cmd[1]);
    if (!zset) {
        return out_err(out, ERR_BAD_TYPE, "expected zset");
    }

    if (limit <= 0) {
        return out_arr(out, 0);
    }

    ZNode *znode = zset_seekge(zset, score, name.data(), name.size());
    znode = znode_offset(znode, offset);

    size_t ctx = out_begin_arr(out);
    int64_t n = 0;
    while (znode && n < limit) {
        out_str(out, znode->name, znode->len);
        out_dbl(out, znode->score);
        znode = znode_offset(znode, 1);
        n += 2;
    }

    out_end_arr(out, ctx, (uint32_t)n);
}

static void do_request(std::vector<std::string> &cmd, Buffer &out) {
    if (cmd.size() == 2 && cmd[0] == "get") {
        return do_get(cmd, out);
    } else if (cmd.size() == 3 && cmd[0] == "set") {
        return do_set(cmd, out);
    } else if (cmd.size() == 2 && cmd[0] == "del") {
        return do_del(cmd, out);
    } else if (cmd.size() == 1 && cmd[0] == "keys") {
        return do_keys(cmd, out);
    } else if (cmd.size() == 4 && cmd[0] == "zadd") {
        return do_zadd(cmd, out);
    } else if (cmd.size() == 3 && cmd[0] == "zrem") {
        return do_zrem(cmd, out);
    } else if (cmd.size() == 3 && cmd[0] == "zscore") {
        return do_zscore(cmd, out);
    } else if (cmd.size() == 6 && cmd[0] == "zquery") {
        return do_zquery(cmd, out);
    } else {
        return out_err(out, ERR_UNKNOWN, "unknown commands");
    }
}

static void response_begin(Buffer &out, size_t *header) {
    *header = out.size();
    buf_append_u32(out, 0);                                     // reserve 4 bytes for response header
}

static size_t response_size(Buffer &out, size_t header) {
    return out.size() - header - 4;
}

static void response_end(Buffer &out, size_t header) {
    size_t msg_size = response_size(out, header);
    if (msg_size > k_max_msg) {
        out.resize(header + 4);
        out_err(out, ERR_TOO_BIG, "response is too big");
        msg_size = response_size(out, header);
    }

    // message header
    uint32_t len = (uint32_t)msg_size;
    memcpy(&out[header], &len, 4);
}

static bool try_one_request(Conn* conn) {
    if (conn->incoming.size() < 4) {
        return false;
    }

    uint32_t len = 0; 
    memcpy(&len, conn->incoming.data(), 4);
    if (len > k_max_msg) {    
        conn->want_close = true;
        return false;
    }

    if (4 + len > conn->incoming.size()) {
        return false;
    }

    const uint8_t* request = &conn->incoming[4];

    // parse the requests 
    std::vector<std::string> cmd;
    if (parse_req(request, len, cmd) < 0) {
        conn->want_close = true;
        printf("error parsing request\n");
        return false;
    }

    // generate response
    size_t header_pos = 0;
    response_begin(conn->outgoing, &header_pos);
    do_request(cmd, conn->outgoing);
    response_end(conn->outgoing, header_pos);

    // clear incoming buffer
    buf_consume(conn->incoming, len + 4);
    return true;
}

static uint32_t handle_accept(int fd) {
    // accept
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);

    int conn_fd = accept(fd, (struct sockaddr*)&client_addr, &addrlen);
    if (conn_fd < 0) {
        return -1;
    }

    // set the new fd connection to non blocking mode
    fd_set_nb(conn_fd);

    Conn* conn = new Conn();
    conn->fd = conn_fd;
    conn->want_read = true;
    conn->last_active_ms = get_monotonic_msec();
    dlist_insert_before(&g_data.idle_list, &conn->idle_node);

    if (g_data.fd2conn.size() <= (size_t)conn->fd) {
        g_data.fd2conn.resize(conn->fd + 1);
    }
    g_data.fd2conn[conn->fd] = conn;

    return 0;
}

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

    if (conn->incoming.size() == 0) {
        conn->want_read = true;
        conn->want_write = false;
    }
}

static void handle_read(Conn* conn) {
    uint8_t buf[64 * 1024];

    ssize_t rv = read(conn->fd, buf, sizeof(buf));
    if (rv <= 0) {                                                      // handle IO Error (rv < 0) and EOF (rv == 0)                  
        conn->want_close = true;
        return;
    }

    buf_append(conn->incoming, buf, rv);

    while (try_one_request(conn)) {}

    // update readiness intention
    if (conn->outgoing.size() > 0) {
        conn->want_read = false;
        conn->want_write = true;

        return handle_write(conn);                                      // optimization for request-response protocol
    }                                                                   // assume client is ready to be written to because it has sent a request
}                                                                       // thus server can write a response without waiting for event loop

const uint64_t k_idle_timeout_ms = 5 * 1000;

static int32_t next_timer_ms() {
    if (dlist_empty(&g_data.idle_list)) {
        return 0;
    }

    uint64_t now_ms = get_monotonic_msec();
    Conn *conn = container_of(g_data.idle_list.next, Conn, idle_node);
    uint64_t next_ms = conn->last_active_ms + k_idle_timeout_ms;

    if (next_ms <= now_ms) {
        return 0;
    }

    return (uint32_t)(next_ms - now_ms);
}

static void process_timers() {
    uint64_t now_ms = get_monotonic_msec();
    while (!dlist_empty(&g_data.idle_list)) {
        Conn *conn = container_of(g_data.idle_list.next, Conn, idle_node);
        uint64_t next_ms = conn->last_active_ms + k_idle_timeout_ms;

        if (next_ms >= now_ms) {
            break;      // not expired
        }

        printf("removing idle connection %d\n", conn->fd);
        conn_destroy(conn);
    }
}

int main(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;                                          // IPv4
    addr.sin_port = htons(1234);                                        // host to network short port 1234
    addr.sin_addr.s_addr = htonl(0);                                    // wildcard IP 0.0.0.0
    
    int rv = bind(fd, (const struct sockaddr*)&addr, sizeof(addr));
    if (rv) {
        perror("bind failed");
        return 1;
    }

    rv = listen(fd, SOMAXCONN);
    if (rv) {
        printf("listen()");
        return 1;
    }

    std::vector<struct pollfd> poll_args;

    while (true) { 
        poll_args.clear();

        struct pollfd pfd = {
            fd,
            POLLIN,                                                     // can read
            0
        };
        poll_args.push_back(pfd);
        
        // update poll() args for existing connections
        for (Conn *conn : g_data.fd2conn) {
            if (!conn)  
                continue;

            struct pollfd pfd = {
                conn->fd,
                POLLERR,                       // notify if socket error
                0
            };

            // set poll() flags
            if (conn->want_read) {
                pfd.events |= POLLIN;
            }
            if (conn->want_write) {
                pfd.events |= POLLOUT;
            }
            poll_args.push_back(pfd);
        }

        int32_t timeout_ms = next_timer_ms();
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), timeout_ms);
        if (rv < 0 && errno == EINTR) {
            continue;                                                   // if received interrupt while waiting for a ready fds
        }
        if (rv < 0) {
            printf("poll() error\n");
            return 1;
        }

        // handle listening socket (server)
        if (poll_args[0].revents) {
            handle_accept(fd);
        }

        // handle client connections
        for (size_t i = 1; i < poll_args.size(); i++) {
            uint32_t ready = poll_args[i].revents;                      // retrieve poll() return
            if (ready == 0) {
                continue;
            }

            Conn *conn = g_data.fd2conn[poll_args[i].fd];
            conn->last_active_ms = get_monotonic_msec();
            dlist_detach(&conn->idle_node);
            dlist_insert_before(&g_data.idle_list, &conn->idle_node);

            if (ready & POLLIN) {
                handle_read(conn);
            }
            if (ready & POLLOUT) {
                handle_write(conn);
            }

            if ((ready & POLLERR) || conn->want_close) {
                conn_destroy(conn);
            }
        }

        process_timers();
    }

    return 0;
}