#ifndef RADIOCAM_UDP_H
#define RADIOCAM_UDP_H

#include <stdint.h>
#include <string.h>

#define RADIOCAM_UDP_MAGIC 0x52435531u /* "RCU1" */
#define RADIOCAM_UDP_VERSION 1u
#define RADIOCAM_UDP_HEADER_LEN 32u
#define RADIOCAM_UDP_FLAG_FRAME_START 0x0001u
#define RADIOCAM_UDP_FLAG_FRAME_END 0x0002u
#define RADIOCAM_UDP_FLAG_ANALYSIS_SAMPLE 0x0004u

#define RADIOCAM_UDP_MTU_1500_PAYLOAD 1456u
#define RADIOCAM_UDP_MTU_9000_PAYLOAD 8960u
#define RADIOCAM_UDP_DEFAULT_PAYLOAD RADIOCAM_UDP_MTU_1500_PAYLOAD
#define RADIOCAM_UDP_MAX_PAYLOAD RADIOCAM_UDP_MTU_9000_PAYLOAD

struct radiocam_udp_header {
    uint32_t magic;
    uint8_t version;
    uint8_t header_len;
    uint16_t flags;
    uint32_t stream_id;
    uint64_t frame_id;
    uint32_t packet_id;
    uint32_t frame_offset;
    uint32_t payload_len;
} __attribute__((packed));

static inline uint16_t radiocam_bswap16(uint16_t v)
{
    return (uint16_t)((v << 8) | (v >> 8));
}

static inline uint32_t radiocam_bswap32(uint32_t v)
{
    return ((v & 0x000000ffu) << 24) |
           ((v & 0x0000ff00u) << 8) |
           ((v & 0x00ff0000u) >> 8) |
           ((v & 0xff000000u) >> 24);
}

static inline uint64_t radiocam_bswap64(uint64_t v)
{
    return ((uint64_t)radiocam_bswap32((uint32_t)v) << 32) |
           radiocam_bswap32((uint32_t)(v >> 32));
}

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define radiocam_htobe16(v) radiocam_bswap16((uint16_t)(v))
#define radiocam_htobe32(v) radiocam_bswap32((uint32_t)(v))
#define radiocam_htobe64(v) radiocam_bswap64((uint64_t)(v))
#define radiocam_be16toh(v) radiocam_bswap16((uint16_t)(v))
#define radiocam_be32toh(v) radiocam_bswap32((uint32_t)(v))
#define radiocam_be64toh(v) radiocam_bswap64((uint64_t)(v))
#else
#define radiocam_htobe16(v) ((uint16_t)(v))
#define radiocam_htobe32(v) ((uint32_t)(v))
#define radiocam_htobe64(v) ((uint64_t)(v))
#define radiocam_be16toh(v) ((uint16_t)(v))
#define radiocam_be32toh(v) ((uint32_t)(v))
#define radiocam_be64toh(v) ((uint64_t)(v))
#endif

static inline void radiocam_udp_header_encode(
    struct radiocam_udp_header *hdr, uint16_t flags, uint32_t stream_id,
    uint64_t frame_id, uint32_t packet_id, uint32_t frame_offset,
    uint32_t payload_len)
{
    hdr->magic = radiocam_htobe32(RADIOCAM_UDP_MAGIC);
    hdr->version = RADIOCAM_UDP_VERSION;
    hdr->header_len = RADIOCAM_UDP_HEADER_LEN;
    hdr->flags = radiocam_htobe16(flags);
    hdr->stream_id = radiocam_htobe32(stream_id);
    hdr->frame_id = radiocam_htobe64(frame_id);
    hdr->packet_id = radiocam_htobe32(packet_id);
    hdr->frame_offset = radiocam_htobe32(frame_offset);
    hdr->payload_len = radiocam_htobe32(payload_len);
}

static inline int radiocam_udp_header_decode(
    const void *buf, size_t len, struct radiocam_udp_header *out)
{
    const struct radiocam_udp_header *in;

    if (len < RADIOCAM_UDP_HEADER_LEN)
        return -1;

    in = (const struct radiocam_udp_header *)buf;
    out->magic = radiocam_be32toh(in->magic);
    out->version = in->version;
    out->header_len = in->header_len;
    out->flags = radiocam_be16toh(in->flags);
    out->stream_id = radiocam_be32toh(in->stream_id);
    out->frame_id = radiocam_be64toh(in->frame_id);
    out->packet_id = radiocam_be32toh(in->packet_id);
    out->frame_offset = radiocam_be32toh(in->frame_offset);
    out->payload_len = radiocam_be32toh(in->payload_len);

    if (out->magic != RADIOCAM_UDP_MAGIC ||
        out->version != RADIOCAM_UDP_VERSION ||
        out->header_len != RADIOCAM_UDP_HEADER_LEN)
        return -1;

    return 0;
}

#endif
