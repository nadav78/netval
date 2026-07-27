/*
 * wire.h — the netval wire format (the "agreed packet format").
 *
 * Every netval packet is a UDP datagram laid out as:
 *
 *      +--------------------+----------------------+
 *      | header (24 bytes)  | payload (0..N bytes) |
 *      +--------------------+----------------------+
 *
 * Exact header byte layout (all multi-byte fields BIG-ENDIAN, i.e.
 * network byte order — most significant byte first):
 *
 *      offset  size  field
 *      ------  ----  -----------------------------------------------
 *        0      4    magic        always 0x4E455456 ("NETV" in ASCII)
 *        4      4    seq          sequence number, starts at 0
 *        8      8    ts_ns        send timestamp, nanoseconds
 *       16      2    payload_len  number of payload bytes that follow
 *       18      2    reserved     always 0 (padding for future use)
 *       20      4    checksum     FNV-1a hash of the payload bytes
 *      ------  ----  -----------------------------------------------
 *       24 bytes total
 *
 * YOU implement the functions declared below, in src/wire.c.
 *
 * Implementation guidance
 * -----------------------
 * Recommended technique: shift-and-mask packing. To write a uint32_t
 * `v` at buf[off] in big-endian order:
 *
 *      buf[off + 0] = (uint8_t)(v >> 24);
 *      buf[off + 1] = (uint8_t)(v >> 16);
 *      buf[off + 2] = (uint8_t)(v >> 8);
 *      buf[off + 3] = (uint8_t)(v);
 *
 * and to read it back:
 *
 *      v = ((uint32_t)buf[off + 0] << 24) | ((uint32_t)buf[off + 1] << 16)
 *        | ((uint32_t)buf[off + 2] << 8)  |  (uint32_t)buf[off + 3];
 *
 * This works on ANY machine regardless of its native endianness — the
 * shifts express the math, not the memory layout. (The alternative for
 * 16/32-bit fields is htonl/htons + memcpy; there is no standard htonll
 * for 64-bit, so for ts_ns shift-and-mask is the way.)
 *
 * Careful with the casts when reading: promote each byte to the target
 * width BEFORE shifting, or `buf[off] << 24` overflows int.
 *
 * FNV-1a (the checksum), full algorithm:
 *      hash = 2166136261u                 (FNV offset basis)
 *      for each byte b:  hash ^= b;  hash *= 16777619u   (FNV prime)
 *      return hash
 */
#ifndef NETVAL_WIRE_H
#define NETVAL_WIRE_H

#include <stddef.h>
#include <stdint.h>

#define NETVAL_MAGIC        0x4E455456u /* "NETV" */
#define NETVAL_HDR_SIZE     24
#define NETVAL_MAX_PAYLOAD  1200
#define NETVAL_MAX_DGRAM    (NETVAL_HDR_SIZE + NETVAL_MAX_PAYLOAD)

/* In-memory representation. Field order here is for humans; the wire
 * layout is defined ONLY by the table above — never by this struct
 * (that would be the struct-cast-onto-buffer trap). */
typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint64_t ts_ns;
    uint16_t payload_len;
    uint32_t checksum;
} netval_hdr;

/* FNV-1a over `len` bytes of `data`. */
uint32_t wire_checksum(const uint8_t *data, size_t len);

/* Serialize `hdr` into buf[0..NETVAL_HDR_SIZE). `buflen` must be at
 * least NETVAL_HDR_SIZE. Returns NETVAL_HDR_SIZE on success, -1 on
 * error (buffer too small, payload_len > NETVAL_MAX_PAYLOAD). */
int wire_hdr_pack(const netval_hdr *hdr, uint8_t *buf, size_t buflen);

/* Deserialize buf[0..NETVAL_HDR_SIZE) into `hdr`. `buflen` is how many
 * bytes were actually received. Returns 0 on success, -1 on error
 * (short buffer, bad magic, payload_len > NETVAL_MAX_PAYLOAD).
 * NOTE: does not verify the checksum — the caller does that once it
 * knows where the payload is. */
int wire_hdr_unpack(const uint8_t *buf, size_t buflen, netval_hdr *hdr);

#endif /* NETVAL_WIRE_H */
