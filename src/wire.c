/* wire.c — wire format implementation.
 *
 * >>> YOURS TO IMPLEMENT <<<
 * The spec (byte layout, packing technique, FNV-1a algorithm) is fully
 * documented in wire.h. Suggested order:
 *   1. wire_checksum   (4 lines — pure math, easy warm-up)
 *   2. wire_hdr_pack   (validate, then shift-and-mask each field to
 *                       its offset from the layout table)
 *   3. wire_hdr_unpack (mirror image: read fields back, THEN validate
 *                       magic and payload_len)
 *
 * The stubs below make the project compile; replace their bodies.
 */
#include "wire.h"

uint32_t wire_checksum(const uint8_t *data, size_t len)
{
    uint32_t hash = 2166136261;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619;
    }
    return hash;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v >> 0);
}  

static void put_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v >> 0);
}

static void put_be64(uint8_t *p, uint64_t v) {
    p[0] = (uint8_t)(v >> 56);
    p[1] = (uint8_t)(v >> 48);
    p[2] = (uint8_t)(v >> 40);
    p[3] = (uint8_t)(v >> 32);
    p[4] = (uint8_t)(v >> 24);
    p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >> 8);
    p[7] = (uint8_t)(v >> 0);
}

int wire_hdr_pack(const netval_hdr *hdr, uint8_t *buf, size_t buflen)
{
    /* TODO(you):
     *   - reject buflen < NETVAL_HDR_SIZE and
     *     hdr->payload_len > NETVAL_MAX_PAYLOAD
     *   - write each field big-endian at its offset (table in wire.h);
     *     don't forget the 2 reserved bytes at offset 18 must be 0
     *   - return NETVAL_HDR_SIZE
     */
    if (buflen < NETVAL_HDR_SIZE || hdr->payload_len > NETVAL_MAX_PAYLOAD) {
        return -1;
    }
    put_be32(buf, hdr->magic);
    put_be32(buf + 4, hdr->seq);
    put_be64(buf + 8, hdr->ts_ns);
    put_be16(buf + 16, hdr->payload_len);
    put_be16(buf + 18, 0);
    put_be32(buf + 20, hdr->checksum);
    return NETVAL_HDR_SIZE;
}

static uint32_t get_be32(const uint8_t *p) {
    uint32_t v = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) 
    | ((uint32_t)p[2] << 8) | ((uint32_t)p[3] << 0);
    return v;
}

static uint16_t get_be16(const uint8_t *p) {
    uint16_t v = ((uint16_t)p[0] << 8) | ((uint16_t)p[1] << 0);
    return v;
}

static uint64_t get_be64(const uint8_t *p) {
    uint64_t v = ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) 
    | ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32)
    | ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16)
    | ((uint64_t)p[6] << 8) | ((uint64_t)p[7] << 0);
    return v;
}

int wire_hdr_unpack(const uint8_t *buf, size_t buflen, netval_hdr *hdr)
{
    /* TODO(you):
     *   - reject buflen < NETVAL_HDR_SIZE
     *   - read each field big-endian (watch the cast-before-shift trap
     *     described in wire.h)
     *   - reject magic != NETVAL_MAGIC and
     *     payload_len > NETVAL_MAX_PAYLOAD
     */
    if (buflen < NETVAL_HDR_SIZE) {
        return -1;
    }
    hdr->magic = get_be32(buf);
    if (hdr->magic != NETVAL_MAGIC) {
        return -1;
    }
    hdr->seq = get_be32(buf + 4);
    hdr->ts_ns = get_be64(buf + 8);
    hdr->payload_len = get_be16(buf + 16);
    if (hdr->payload_len > NETVAL_MAX_PAYLOAD) {
        return -1;
    }
    hdr->checksum = get_be32(buf + 20);
    return 0;
}
