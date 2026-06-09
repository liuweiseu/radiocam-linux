#define _POSIX_C_SOURCE 200809L

#include "radiocam_udp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_PORT 50000
#define MAX_PACKET (RADIOCAM_UDP_HEADER_LEN + RADIOCAM_UDP_MAX_PAYLOAD)

struct options {
    const char *bind_addr;
    int port;
    int rcvbuf;
    double stats_interval;
};

struct stream_state {
    bool have_packet;
    bool have_frame;
    uint32_t stream_id;
    uint64_t frame_id;
    uint32_t next_packet_id;
    uint32_t expected_offset;
    uint64_t packet_gaps;
    uint64_t frame_gaps;
    uint64_t frames_complete;
    uint64_t frames_started;
};

struct stats {
    uint64_t packets;
    uint64_t bad_packets;
    uint64_t bytes;
    struct timespec start;
    struct timespec last;
    uint64_t last_packets;
    uint64_t last_bytes;
};

static volatile sig_atomic_t running = 1;

static void on_signal(int signum)
{
    (void)signum;
    running = 0;
}

static double elapsed_sec(const struct timespec *a, const struct timespec *b)
{
    return (double)(b->tv_sec - a->tv_sec) +
           (double)(b->tv_nsec - a->tv_nsec) / 1e9;
}

static uint64_t parse_u64(const char *s, const char *name)
{
    char *end = NULL;
    unsigned long long v;

    errno = 0;
    v = strtoull(s, &end, 0);
    if (errno || !end || *end) {
        fprintf(stderr, "invalid %s: %s\n", name, s);
        exit(2);
    }
    return (uint64_t)v;
}

static void usage(FILE *f, const char *prog)
{
    fprintf(f,
            "Usage: %s [options]\n"
            "  -b, --bind ADDR            bind IPv4 address [0.0.0.0]\n"
            "  -p, --port PORT            UDP port [%d]\n"
            "      --rcvbuf BYTES         SO_RCVBUF size\n"
            "      --stats-interval SEC   stats print interval [1.0]\n",
            prog, DEFAULT_PORT);
}

static void parse_options(int argc, char **argv, struct options *opt)
{
    int i;

    opt->bind_addr = "0.0.0.0";
    opt->port = DEFAULT_PORT;
    opt->rcvbuf = 0;
    opt->stats_interval = 1.0;

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *val;

        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
            usage(stdout, argv[0]);
            exit(0);
        }
        if (!strcmp(arg, "-b") || !strcmp(arg, "--bind") ||
            !strcmp(arg, "-p") || !strcmp(arg, "--port") ||
            !strcmp(arg, "--rcvbuf") || !strcmp(arg, "--stats-interval")) {
            if (++i >= argc) {
                fprintf(stderr, "missing value for %s\n", arg);
                exit(2);
            }
            val = argv[i];
        } else {
            fprintf(stderr, "unknown option: %s\n", arg);
            usage(stderr, argv[0]);
            exit(2);
        }

        if (!strcmp(arg, "-b") || !strcmp(arg, "--bind"))
            opt->bind_addr = val;
        else if (!strcmp(arg, "-p") || !strcmp(arg, "--port"))
            opt->port = (int)parse_u64(val, "port");
        else if (!strcmp(arg, "--rcvbuf"))
            opt->rcvbuf = (int)parse_u64(val, "rcvbuf");
        else if (!strcmp(arg, "--stats-interval"))
            opt->stats_interval = atof(val);
    }

    if (opt->port <= 0 || opt->port > 65535) {
        fprintf(stderr, "port out of range\n");
        exit(2);
    }
    if (opt->stats_interval <= 0.0)
        opt->stats_interval = 1.0;
}

static int bind_socket(const struct options *opt)
{
    int fd;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd == -1) {
        perror("socket");
        exit(1);
    }
    if (opt->rcvbuf > 0 &&
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &opt->rcvbuf, sizeof(opt->rcvbuf)) == -1)
        perror("setsockopt SO_RCVBUF");

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)opt->port);
    if (inet_pton(AF_INET, opt->bind_addr, &addr.sin_addr) != 1) {
        fprintf(stderr, "invalid bind address: %s\n", opt->bind_addr);
        exit(1);
    }
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        exit(1);
    }
    return fd;
}

static void update_stream(struct stream_state *s,
                          const struct radiocam_udp_header *h)
{
    if (!s->have_packet || s->stream_id != h->stream_id) {
        memset(s, 0, sizeof(*s));
        s->stream_id = h->stream_id;
    }

    if (h->flags & RADIOCAM_UDP_FLAG_FRAME_START) {
        if (s->have_frame && h->frame_id > s->frame_id + 1)
            s->frame_gaps += h->frame_id - s->frame_id - 1;
        s->have_frame = true;
        s->frame_id = h->frame_id;
        s->next_packet_id = 0;
        s->expected_offset = 0;
        s->frames_started++;
    } else if (!s->have_frame || h->frame_id != s->frame_id) {
        if (s->have_frame && h->frame_id > s->frame_id + 1)
            s->frame_gaps += h->frame_id - s->frame_id - 1;
        s->have_frame = true;
        s->frame_id = h->frame_id;
        s->next_packet_id = h->packet_id;
        s->expected_offset = h->frame_offset;
    }

    if (h->packet_id != s->next_packet_id) {
        if (h->packet_id > s->next_packet_id)
            s->packet_gaps += h->packet_id - s->next_packet_id;
        s->next_packet_id = h->packet_id;
    }
    if (h->frame_offset != s->expected_offset)
        s->packet_gaps++;

    s->next_packet_id++;
    s->expected_offset = h->frame_offset + h->payload_len;
    if (h->flags & RADIOCAM_UDP_FLAG_FRAME_END)
        s->frames_complete++;
    s->have_packet = true;
}

static void print_stats(const struct stats *st, const struct stream_state *ss)
{
    struct timespec now;
    double dt;
    double total;
    uint64_t dpackets, dbytes;

    clock_gettime(CLOCK_MONOTONIC, &now);
    dt = elapsed_sec(&st->last, &now);
    total = elapsed_sec(&st->start, &now);
    if (dt <= 0.0)
        return;
    dpackets = st->packets - st->last_packets;
    dbytes = st->bytes - st->last_bytes;

    fprintf(stderr,
            "rx %.1fs packets=%llu bad=%llu rate=%.1f pkt/s %.2f MB/s "
            "frames=%llu complete=%llu frame_gaps=%llu packet_gaps=%llu\n",
            total, (unsigned long long)st->packets,
            (unsigned long long)st->bad_packets, (double)dpackets / dt,
            (double)dbytes / dt / 1e6,
            (unsigned long long)ss->frames_started,
            (unsigned long long)ss->frames_complete,
            (unsigned long long)ss->frame_gaps,
            (unsigned long long)ss->packet_gaps);
}

int main(int argc, char **argv)
{
    struct options opt;
    struct stats st;
    struct stream_state ss;
    int fd;
    uint8_t packet[MAX_PACKET];

    parse_options(argc, argv, &opt);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    fd = bind_socket(&opt);

    memset(&st, 0, sizeof(st));
    memset(&ss, 0, sizeof(ss));
    clock_gettime(CLOCK_MONOTONIC, &st.start);
    st.last = st.start;

    while (running) {
        struct radiocam_udp_header h;
        ssize_t n = recv(fd, packet, sizeof(packet), 0);
        struct timespec now;

        if (n == -1) {
            if (errno == EINTR)
                continue;
            perror("recv");
            break;
        }
        if (radiocam_udp_header_decode(packet, (size_t)n, &h) == -1 ||
            h.payload_len + h.header_len != (uint32_t)n) {
            st.bad_packets++;
        } else {
            st.packets++;
            st.bytes += h.payload_len;
            update_stream(&ss, &h);
        }

        clock_gettime(CLOCK_MONOTONIC, &now);
        if (elapsed_sec(&st.last, &now) >= opt.stats_interval) {
            print_stats(&st, &ss);
            st.last = now;
            st.last_packets = st.packets;
            st.last_bytes = st.bytes;
        }
    }

    print_stats(&st, &ss);
    close(fd);
    return 0;
}
