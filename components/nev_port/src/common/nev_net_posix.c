/*
 * One socket implementation for both targets.
 *
 * ESP-IDF's lwIP provides the BSD socket API under the standard headers, so
 * this file compiles unchanged for the ESP32-S3 and for the host. That is worth
 * stating because it is unusual: almost everything else in nev_port is forked
 * between src/host/ and src/esp32s3/.
 *
 * The two real differences:
 *
 *   1. Link state. The host is always "up"; the device is up only once Wi-Fi
 *      has an address, which nev_board reports through nev_net_set_up().
 *   2. lwIP's default socket count is small (CONFIG_LWIP_MAX_SOCKETS, 10 by
 *      default). The bridge uses two — one TCP, one UDP for discovery — which
 *      is why neither is left open "just in case".
 */
#include "nev_port/nev_net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "nev_port/nev_log.h"

static bool s_link_up =
#ifdef NEV_TARGET_HOST
    true;
#else
    false;
#endif

/* Called by nev_board when Wi-Fi gets or loses an address. */
void nev_net_set_up(bool up) {
    s_link_up = up;
}

bool nev_net_is_up(void) {
    return s_link_up;
}

static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

nev_socket_t nev_net_connect(const char *ipv4, uint16_t port) {
    if (!ipv4) return NEV_SOCKET_INVALID;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return NEV_SOCKET_INVALID;

    if (!set_nonblocking(fd)) {
        close(fd);
        return NEV_SOCKET_INVALID;
    }
    /* Replies arrive a few tokens at a time. Nagle would batch them into a
     * stutter visible on a face animating at 30 fps. */
    int one = 1;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ipv4, &addr.sin_addr) != 1) {
        close(fd);
        return NEV_SOCKET_INVALID;
    }

    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS && errno != EALREADY) {
        close(fd);
        return NEV_SOCKET_INVALID;
    }
    return fd;
}

nev_conn_state_t nev_net_connect_poll(nev_socket_t sock) {
    if (sock < 0) return NEV_CONN_FAILED;

    /* The portable way to ask "did the connect finish": a zero-timeout select
     * for writability, then the socket's own error. Writable alone is not
     * enough — a refused connection is also reported as writable. */
    fd_set wr;
    FD_ZERO(&wr);
    FD_SET(sock, &wr);
    struct timeval zero = {0, 0};

    int ready = select(sock + 1, NULL, &wr, NULL, &zero);
    if (ready < 0) return (errno == EINTR) ? NEV_CONN_PENDING : NEV_CONN_FAILED;
    if (ready == 0) return NEV_CONN_PENDING;

    int err = 0;
    socklen_t len = sizeof(err);
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len) < 0) return NEV_CONN_FAILED;
    if (err != 0) return NEV_CONN_FAILED;
    return NEV_CONN_READY;
}

int nev_net_send(nev_socket_t sock, const uint8_t *data, size_t len) {
    if (sock < 0 || !data) return -1;
#ifdef MSG_NOSIGNAL
    /* Without this, writing to a closed socket kills the process with SIGPIPE
     * on the host. On lwIP the flag is a no-op. */
    ssize_t n = send(sock, data, len, MSG_NOSIGNAL);
#else
    ssize_t n = send(sock, data, len, 0);
#endif
    if (n >= 0) return (int)n;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    return -1;
}

int nev_net_recv(nev_socket_t sock, uint8_t *out, size_t max) {
    if (sock < 0 || !out) return -1;
    ssize_t n = recv(sock, out, max, 0);
    if (n > 0) return (int)n;
    /* Zero from recv is the peer closing, which is a failure to the caller —
     * distinct from "nothing to read", which is EAGAIN. */
    if (n == 0) return -1;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    return -1;
}

void nev_net_close(nev_socket_t sock) {
    if (sock >= 0) close(sock);
}

nev_socket_t nev_net_udp_open(uint16_t port, const char *mcast_ipv4) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return NEV_SOCKET_INVALID;

    /*
     * Both reuse options, because mDNS requires sharing port 5353 with whatever
     * responder the machine is already running — avahi on Linux, mDNSResponder
     * on macOS. SO_REUSEADDR alone is enough on some stacks and not on others;
     * SO_REUSEPORT does not exist on lwIP, hence the guard.
     */
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return NEV_SOCKET_INVALID;
    }

    if (mcast_ipv4) {
        struct ip_mreq mreq;
        memset(&mreq, 0, sizeof(mreq));
        if (inet_pton(AF_INET, mcast_ipv4, &mreq.imr_multiaddr) != 1) {
            close(fd);
            return NEV_SOCKET_INVALID;
        }
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            /* Not fatal: a network that blocks multicast still works if the
             * daemon address is configured by hand. */
            NEV_LOGW("net", "could not join %s; mDNS discovery will not work", mcast_ipv4);
        }
    }

    if (!set_nonblocking(fd)) {
        close(fd);
        return NEV_SOCKET_INVALID;
    }
    return fd;
}

int nev_net_sendto(nev_socket_t sock, const char *ipv4, uint16_t port, const uint8_t *data,
                   size_t len) {
    if (sock < 0 || !ipv4 || !data) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ipv4, &addr.sin_addr) != 1) return -1;

    ssize_t n = sendto(sock, data, len, 0, (struct sockaddr *)&addr, sizeof(addr));
    if (n >= 0) return (int)n;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    return -1;
}

int nev_net_recvfrom(nev_socket_t sock, uint8_t *out, size_t max, nev_endpoint_t *from) {
    if (sock < 0 || !out) return -1;

    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    ssize_t n = recvfrom(sock, out, max, 0, (struct sockaddr *)&addr, &addr_len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
        return -1;
    }
    if (from) {
        from->ipv4 = ntohl(addr.sin_addr.s_addr);
        from->port = ntohs(addr.sin_port);
    }
    return (int)n;
}

void nev_net_ipv4_str(uint32_t ipv4, char *out, size_t out_len) {
    if (!out || out_len == 0) return;
    snprintf(out, out_len, "%u.%u.%u.%u", (unsigned)((ipv4 >> 24) & 0xFF),
             (unsigned)((ipv4 >> 16) & 0xFF), (unsigned)((ipv4 >> 8) & 0xFF),
             (unsigned)(ipv4 & 0xFF));
}
