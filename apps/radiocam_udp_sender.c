#define _GNU_SOURCE

#include "radiocam_udp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <netinet/in.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#ifndef V4L2_MAX_PLANES
#define V4L2_MAX_PLANES 8
#endif

#define DEFAULT_DEVICE "/dev/video0"
#define DEFAULT_PORT 50000
#define DEFAULT_BUFFERS 8
#define MAX_BATCH 1024

struct buffer {
    void *start;
    size_t length;
};

struct options {
    const char *device;
    const char *dest;
    int port;
    uint32_t stream_id;
    unsigned int buffers;
    uint32_t payload_bytes;
    int sndbuf;
    uint64_t frame_limit;
    int cpu;
    double stats_interval;
    const char *analysis_dest;
    int analysis_port;
    unsigned int analysis_every;
};

struct stats {
    uint64_t frames;
    uint64_t packets;
    uint64_t bytes;
    uint64_t sampled_packets;
    struct timespec start;
    struct timespec last;
    uint64_t last_frames;
    uint64_t last_packets;
    uint64_t last_bytes;
};

struct analysis_entry {
    unsigned int hdr_idx;
    size_t payload_offset;
    size_t payload_len;
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

static int xioctl(int fd, unsigned long request, void *arg)
{
    int ret;
    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

static void usage(FILE *f, const char *prog)
{
    fprintf(f,
            "Usage: %s -a DEST [options]\n"
            "  -d, --device PATH          V4L2 device [%s]\n"
            "  -a, --dest ADDR            UDP destination IPv4 address\n"
            "  -p, --port PORT            UDP destination port [%d]\n"
            "  -s, --stream-id ID         stream id [0]\n"
            "  -b, --buffers N            MMAP buffer count [%d]\n"
            "  -m, --payload-bytes N      UDP payload bytes [%u]\n"
            "      --sndbuf BYTES         SO_SNDBUF size\n"
            "      --frames N             stop after N frames\n"
            "      --cpu N                pin process to CPU N\n"
            "      --stats-interval SEC   stats print interval [1.0]\n"
            "      --analysis-dest ADDR   sampled-analysis destination [127.0.0.1]\n"
            "      --analysis-port PORT   sampled-analysis port; 0 disables [0]\n"
            "      --analysis-every N     copy every Nth packet to analysis [1000]\n",
            prog, DEFAULT_DEVICE, DEFAULT_PORT, DEFAULT_BUFFERS,
            RADIOCAM_UDP_DEFAULT_PAYLOAD);
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

static void parse_options(int argc, char **argv, struct options *opt)
{
    int i;

    opt->device = DEFAULT_DEVICE;
    opt->dest = NULL;
    opt->port = DEFAULT_PORT;
    opt->stream_id = 0;
    opt->buffers = DEFAULT_BUFFERS;
    opt->payload_bytes = RADIOCAM_UDP_DEFAULT_PAYLOAD;
    opt->sndbuf = 0;
    opt->frame_limit = 0;
    opt->cpu = -1;
    opt->stats_interval = 1.0;
    opt->analysis_dest = "127.0.0.1";
    opt->analysis_port = 0;
    opt->analysis_every = 1000;

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *val = NULL;

        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
            usage(stdout, argv[0]);
            exit(0);
        }

        if (!strcmp(arg, "-d") || !strcmp(arg, "--device") ||
            !strcmp(arg, "-a") || !strcmp(arg, "--dest") ||
            !strcmp(arg, "-p") || !strcmp(arg, "--port") ||
            !strcmp(arg, "-s") || !strcmp(arg, "--stream-id") ||
            !strcmp(arg, "-b") || !strcmp(arg, "--buffers") ||
            !strcmp(arg, "-m") || !strcmp(arg, "--payload-bytes") ||
            !strcmp(arg, "--sndbuf") || !strcmp(arg, "--frames") ||
            !strcmp(arg, "--cpu") || !strcmp(arg, "--stats-interval") ||
            !strcmp(arg, "--analysis-dest") || !strcmp(arg, "--analysis-port") ||
            !strcmp(arg, "--analysis-every")) {
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

        if (!strcmp(arg, "-d") || !strcmp(arg, "--device"))
            opt->device = val;
        else if (!strcmp(arg, "-a") || !strcmp(arg, "--dest"))
            opt->dest = val;
        else if (!strcmp(arg, "-p") || !strcmp(arg, "--port"))
            opt->port = (int)parse_u64(val, "port");
        else if (!strcmp(arg, "-s") || !strcmp(arg, "--stream-id"))
            opt->stream_id = (uint32_t)parse_u64(val, "stream-id");
        else if (!strcmp(arg, "-b") || !strcmp(arg, "--buffers"))
            opt->buffers = (unsigned int)parse_u64(val, "buffers");
        else if (!strcmp(arg, "-m") || !strcmp(arg, "--payload-bytes"))
            opt->payload_bytes = (uint32_t)parse_u64(val, "payload-bytes");
        else if (!strcmp(arg, "--sndbuf"))
            opt->sndbuf = (int)parse_u64(val, "sndbuf");
        else if (!strcmp(arg, "--frames"))
            opt->frame_limit = parse_u64(val, "frames");
        else if (!strcmp(arg, "--cpu"))
            opt->cpu = (int)parse_u64(val, "cpu");
        else if (!strcmp(arg, "--stats-interval"))
            opt->stats_interval = atof(val);
        else if (!strcmp(arg, "--analysis-dest"))
            opt->analysis_dest = val;
        else if (!strcmp(arg, "--analysis-port"))
            opt->analysis_port = (int)parse_u64(val, "analysis-port");
        else if (!strcmp(arg, "--analysis-every"))
            opt->analysis_every = (unsigned int)parse_u64(val, "analysis-every");
    }

    if (!opt->dest) {
        fprintf(stderr, "--dest is required\n");
        usage(stderr, argv[0]);
        exit(2);
    }
    if (opt->port <= 0 || opt->port > 65535 ||
        opt->analysis_port < 0 || opt->analysis_port > 65535) {
        fprintf(stderr, "port out of range\n");
        exit(2);
    }
    if (opt->buffers < 2 || opt->buffers > 64) {
        fprintf(stderr, "buffers must be between 2 and 64\n");
        exit(2);
    }
    if (!opt->payload_bytes || opt->payload_bytes > RADIOCAM_UDP_MAX_PAYLOAD) {
        fprintf(stderr, "payload-bytes must be 1..%u\n", RADIOCAM_UDP_MAX_PAYLOAD);
        exit(2);
    }
    if (opt->stats_interval <= 0.0)
        opt->stats_interval = 1.0;
}

static void maybe_pin_cpu(int cpu)
{
    cpu_set_t set;

    if (cpu < 0)
        return;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set) == -1)
        perror("sched_setaffinity");
}

static int make_udp_socket(const char *host, int port, int sndbuf,
                           struct sockaddr_in *addr)
{
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);

    if (fd == -1) {
        perror("socket");
        exit(1);
    }
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr->sin_addr) != 1) {
        fprintf(stderr, "invalid IPv4 address: %s\n", host);
        exit(1);
    }
    if (sndbuf > 0 &&
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) == -1)
        perror("setsockopt SO_SNDBUF");
    return fd;
}

static struct radiocam_shm_stats *setup_shm_stats(void)
{
    int fd;
    void *p;
    struct radiocam_shm_stats *s;

    fd = shm_open(RADIOCAM_UDP_SHM_NAME, O_CREAT | O_RDWR, 0644);
    if (fd == -1) {
        perror("shm_open");
        return NULL;
    }
    if (ftruncate(fd, (off_t)sizeof(struct radiocam_shm_stats)) == -1) {
        perror("ftruncate shm");
        close(fd);
        return NULL;
    }
    p = mmap(NULL, sizeof(struct radiocam_shm_stats),
             PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) {
        perror("mmap shm");
        return NULL;
    }
    s = (struct radiocam_shm_stats *)p;
    memset(s, 0, sizeof(*s));
    s->shm_version = RADIOCAM_UDP_SHM_VERSION;
    return s;
}

static void setup_v4l2(int fd, struct buffer **buffers_out,
                       unsigned int *count_out)
{
    struct v4l2_requestbuffers req;
    struct buffer *buffers;
    unsigned int i;

    memset(&req, 0, sizeof(req));
    req.count = *count_out;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
        perror("VIDIOC_REQBUFS");
        exit(1);
    }
    if (req.count < 2) {
        fprintf(stderr, "driver returned too few buffers: %u\n", req.count);
        exit(1);
    }

    buffers = calloc(req.count, sizeof(*buffers));
    if (!buffers) {
        perror("calloc buffers");
        exit(1);
    }

    for (i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[V4L2_MAX_PLANES];

        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = V4L2_MAX_PLANES;
        buf.m.planes = planes;
        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
            perror("VIDIOC_QUERYBUF");
            exit(1);
        }
        buffers[i].length = planes[0].length;
        buffers[i].start = mmap(NULL, planes[0].length, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, planes[0].m.mem_offset);
        if (buffers[i].start == MAP_FAILED) {
            perror("mmap");
            exit(1);
        }
    }

    for (i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[V4L2_MAX_PLANES];

        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = V4L2_MAX_PLANES;
        buf.m.planes = planes;
        if (xioctl(fd, VIDIOC_QBUF, &buf) == -1) {
            perror("VIDIOC_QBUF");
            exit(1);
        }
    }

    *buffers_out = buffers;
    *count_out = req.count;
}

static void print_stats(struct stats *st, const char *prefix)
{
    struct timespec now;
    double dt;
    double total;
    uint64_t dpackets, dbytes;

    clock_gettime(CLOCK_MONOTONIC, &now);
    dt = elapsed_sec(&st->last, &now);
    if (dt <= 0.0)
        return;
    total = elapsed_sec(&st->start, &now);
    dpackets = st->packets - st->last_packets;
    dbytes = st->bytes - st->last_bytes;
    fprintf(stderr,
            "%s %.1fs frames=%llu packets=%llu rate=%.1f pkt/s %.2f MB/s sampled=%llu\n",
            prefix, total, (unsigned long long)st->frames,
            (unsigned long long)st->packets, (double)dpackets / dt,
            (double)dbytes / dt / 1e6,
            (unsigned long long)st->sampled_packets);
    st->last = now;
    st->last_frames = st->frames;
    st->last_packets = st->packets;
    st->last_bytes = st->bytes;
}

static void send_analysis_packet(int fd, const struct sockaddr_in *addr,
                                 const struct radiocam_udp_header *hdr,
                                 const void *payload, size_t payload_len)
{
    struct iovec iov[2];
    struct msghdr msg;

    if (!addr)
        return;
    memset(&msg, 0, sizeof(msg));
    iov[0].iov_base = (void *)hdr;
    iov[0].iov_len = sizeof(*hdr);
    iov[1].iov_base = (void *)payload;
    iov[1].iov_len = payload_len;
    msg.msg_name = (void *)addr;
    msg.msg_namelen = sizeof(*addr);
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;
    if (sendmsg(fd, &msg, MSG_DONTWAIT) == -1 &&
        errno != EAGAIN && errno != EWOULDBLOCK)
        perror("sendmsg analysis");
}

static void stream_loop(int vfd, int udp_fd, const struct sockaddr_in *dst,
                        int analysis_fd, const struct sockaddr_in *analysis_dst,
                        struct radiocam_shm_stats *shm,
                        const struct options *opt)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    struct buffer *buffers = NULL;
    unsigned int buffer_count = opt->buffers;
    uint64_t frame_id = 0;
    struct stats st;

    setup_v4l2(vfd, &buffers, &buffer_count);

    if (xioctl(vfd, VIDIOC_STREAMON, &type) == -1) {
        perror("VIDIOC_STREAMON");
        exit(1);
    }

    memset(&st, 0, sizeof(st));
    clock_gettime(CLOCK_MONOTONIC, &st.start);
    st.last = st.start;

    while (running && (!opt->frame_limit || frame_id < opt->frame_limit)) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[V4L2_MAX_PLANES];
        size_t bytesused;
        uint8_t *base;
        size_t offset = 0;
        uint32_t packet_in_frame = 0;
        uint64_t frame_timestamp_us;
        uint64_t frame_cal_count = 0;

        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.length = V4L2_MAX_PLANES;
        buf.m.planes = planes;

        if (xioctl(vfd, VIDIOC_DQBUF, &buf) == -1) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN) {
                struct pollfd pfd = {.fd = vfd, .events = POLLIN};
                if (poll(&pfd, 1, 1000) == -1 && errno != EINTR) {
                    perror("poll V4L2");
                    break;
                }
                continue;
            }
            perror("VIDIOC_DQBUF");
            break;
        }
        if (buf.index >= buffer_count) {
            fprintf(stderr, "driver returned invalid buffer index %u\n", buf.index);
            break;
        }

        frame_timestamp_us = (uint64_t)buf.timestamp.tv_sec * 1000000ULL +
                             (uint64_t)buf.timestamp.tv_usec;
        bytesused = planes[0].bytesused;
        base = buffers[buf.index].start;

        /* First 8 bytes of every frame are the FPGA calibrated_sample_count
         * emitted as a CSI-2 embedded data line (little-endian uint64). */
        if (bytesused >= 8) {
            memcpy(&frame_cal_count, base, sizeof(frame_cal_count));
            base += 8;
            bytesused -= 8;
        }

        while (offset < bytesused && running) {
            struct mmsghdr msgs[MAX_BATCH];
            struct iovec iovs[MAX_BATCH][2];
            struct radiocam_udp_header hdrs[MAX_BATCH];
            struct analysis_entry aq[MAX_BATCH];
            size_t lens[MAX_BATCH];
            unsigned int n = 0;
            unsigned int n_aq = 0;
            int sent;

            memset(msgs, 0, sizeof(msgs));
            while (n < MAX_BATCH && offset < bytesused) {
                size_t remaining = bytesused - offset;
                uint32_t chunk = remaining > opt->payload_bytes
                    ? opt->payload_bytes
                    : (uint32_t)remaining;
                uint16_t flags = 0;

                if (offset == 0)
                    flags |= RADIOCAM_UDP_FLAG_FRAME_START;
                if (offset + chunk == bytesused)
                    flags |= RADIOCAM_UDP_FLAG_FRAME_END;

                radiocam_udp_header_encode(&hdrs[n], flags, opt->stream_id,
                                              frame_id, packet_in_frame,
                                              (uint32_t)offset, chunk,
                                              frame_timestamp_us, frame_cal_count);
                iovs[n][0].iov_base = &hdrs[n];
                iovs[n][0].iov_len = sizeof(hdrs[n]);
                iovs[n][1].iov_base = base + offset;
                iovs[n][1].iov_len = chunk;
                msgs[n].msg_hdr.msg_name = (void *)dst;
                msgs[n].msg_hdr.msg_namelen = sizeof(*dst);
                msgs[n].msg_hdr.msg_iov = iovs[n];
                msgs[n].msg_hdr.msg_iovlen = 2;
                lens[n] = chunk;

                if (analysis_fd >= 0 && opt->analysis_every &&
                    (st.packets + n) % opt->analysis_every == 0) {
                    aq[n_aq].hdr_idx = n;
                    aq[n_aq].payload_offset = offset;
                    aq[n_aq].payload_len = chunk;
                    n_aq++;
                    st.sampled_packets++;
                    if (shm)
                        atomic_fetch_add_explicit(&shm->sampled_packets, 1,
                                                  memory_order_relaxed);
                }

                offset += chunk;
                packet_in_frame++;
                n++;
            }

            sent = sendmmsg(udp_fd, msgs, n, 0);
            if (sent == -1) {
                perror("sendmmsg");
                running = 0;
                break;
            }
            if ((unsigned int)sent != n) {
                fprintf(stderr, "short sendmmsg: %d/%u\n", sent, n);
                running = 0;
                break;
            }
            {
                uint64_t batch_bytes = 0;
                for (int i = 0; i < sent; i++) {
                    batch_bytes += lens[i];
                }
                st.bytes += batch_bytes;
                st.packets += (uint64_t)sent;
                if (shm) {
                    atomic_fetch_add_explicit(&shm->packets, (uint64_t)sent,
                                              memory_order_relaxed);
                    atomic_fetch_add_explicit(&shm->bytes, batch_bytes,
                                              memory_order_relaxed);
                }
            }

            /* Drain the analysis queue after the main send completes. */
            for (unsigned int ai = 0; ai < n_aq; ai++) {
                unsigned int i = aq[ai].hdr_idx;
                struct radiocam_udp_header ahdr;
                radiocam_udp_header_encode(
                    &ahdr,
                    radiocam_be16toh(hdrs[i].flags) | RADIOCAM_UDP_FLAG_ANALYSIS_SAMPLE,
                    opt->stream_id, frame_id,
                    radiocam_be32toh(hdrs[i].packet_id),
                    radiocam_be32toh(hdrs[i].frame_offset),
                    (uint32_t)aq[ai].payload_len,
                    frame_timestamp_us, frame_cal_count);
                send_analysis_packet(analysis_fd, analysis_dst, &ahdr,
                                     base + aq[ai].payload_offset,
                                     aq[ai].payload_len);
            }
        }

        st.frames++;
        if (shm)
            atomic_fetch_add_explicit(&shm->frames, 1, memory_order_relaxed);

        if (xioctl(vfd, VIDIOC_QBUF, &buf) == -1) {
            perror("VIDIOC_QBUF");
            break;
        }

        frame_id++;
        {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (elapsed_sec(&st.last, &now) >= opt->stats_interval)
                print_stats(&st, "tx");
        }
    }

    xioctl(vfd, VIDIOC_STREAMOFF, &type);
    print_stats(&st, "tx");
}

int main(int argc, char **argv)
{
    struct options opt;
    struct sockaddr_in dst, analysis_dst;
    struct radiocam_shm_stats *shm;
    int vfd;
    int udp_fd;
    int analysis_fd = -1;

    parse_options(argc, argv, &opt);
    maybe_pin_cpu(opt.cpu);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    vfd = open(opt.device, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (vfd == -1) {
        perror(opt.device);
        return 1;
    }

    udp_fd = make_udp_socket(opt.dest, opt.port, opt.sndbuf, &dst);
    if (opt.analysis_port > 0)
        analysis_fd = make_udp_socket(opt.analysis_dest, opt.analysis_port, 0,
                                      &analysis_dst);

    shm = setup_shm_stats();

    stream_loop(vfd, udp_fd, &dst, analysis_fd,
                analysis_fd >= 0 ? &analysis_dst : NULL,
                shm, &opt);

    if (shm)
        munmap(shm, sizeof(*shm));
    shm_unlink(RADIOCAM_UDP_SHM_NAME);

    close(udp_fd);
    if (analysis_fd >= 0)
        close(analysis_fd);
    close(vfd);
    return 0;
}
