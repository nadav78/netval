# netval — Progress & Learning Log

Living document tracking the milestone plan, who built each piece
(per the ownership rules in CLAUDE.md), and what Nadav learned along
the way. Claude updates this as work lands.

**Ownership legend:** 🧑 Nadav (core logic — interview material) · 🤖 Claude (permitted boilerplate) · 📋 Claude spec / Nadav implementation

---

## Milestone Overview

| # | Milestone                                  | Status         |
|---|--------------------------------------------|----------------|
| 1 | Skeleton & Wire Format                     | 🔨 In progress |
| 2 | Event-Driven Receiver (epoll)              | ⬜ Not started |
| 3 | Multithreaded Generator & Verification     | ⬜ Not started |
| 4 | Python Test Harness                        | ⬜ Not started |

---

## Core Skills Tracker

Foundational low-level skills this project drills. Workflow: when one
comes up, Nadav attempts it solo first; afterwards Claude names which
skill was practiced, its general use case, and how it shows up in
interviews (see CLAUDE.md).

1. **Fixed-width integers & overflow** — values wrap mod 2^N; unsigned
   wrap is defined, signed overflow is UB. Use case: checksums,
   counters, sequence-number math. Interviews: "what happens when this
   overflows?" and UB questions. *Practiced: wire_checksum ✅*
2. **Bitwise operations (`&`, `|`, `^`, `<<`, `>>`)** — building and
   inspecting values bit by bit. Use case: flag fields, device
   registers, protocol headers, masks. Interviews: classic bit-
   manipulation warm-ups; reading real header-parsing code.
   *Practiced: wire_checksum (XOR), unpack getters (OR-merge) ✅*
3. **Shift-and-mask byte packing** — extracting/placing bytes of a wide
   value at exact buffer offsets, endianness-safe. Use case:
   serializing any binary format (network headers, file formats,
   embedded protocols). Interviews: "parse this header" /
   "implement htonl" exercises. *Practiced: wire_hdr_pack + unpack,
   both directions ✅ (incl. integer-promotion trap)*
4. **Hex fluency** — reading `0xA4` as one byte, a `uint32_t` as 8 hex
   digits, left end = high byte. Use case: reading dumps, addresses,
   masks, debugger output. Interviews: rarely asked directly, but
   assumed everywhere. *Practiced: ongoing*

---

## Milestone 1 — Skeleton & Wire Format

**Goal:** runnable `netval` binary with tx/rx modes; single-threaded
blocking UDP send/receive; agreed 24-byte wire format with sequence
numbers and FNV-1a checksum.
**Exit criteria:** tx sends 1000 sequenced packets to rx on localhost,
rx reports 1000 received; ASan clean.

| Piece                                    | Owner | Status  |
|------------------------------------------|-------|---------|
| `Makefile` (release/debug/asan/tsan)     | 🤖    | ✅ Done |
| `CLAUDE.md`, `PROGRESS.md`               | 🤖    | ✅ Done |
| `src/cli.{h,c}` — getopt_long parsing    | 🤖    | ✅ Done |
| `src/log.{h,c}` — timestamped formatter  | 🤖    | ✅ Done |
| `src/main.c` — dispatch glue             | 🤖    | ✅ Done |
| `src/wire.h` — wire format spec          | 📋    | ✅ Done |
| `wire_checksum` (FNV-1a)                 | 🧑    | ✅ Done |
| `wire_hdr_pack` / `wire_hdr_unpack`      | 🧑    | ✅ Done |
| `src/tx.c` — blocking UDP sender         | 🧑    | 🔨 Next up |
| `src/rx.c` — blocking UDP receiver       | 🧑    | ⬜ Not started |
| End-to-end localhost run, ASan clean     | 🧑    | ⬜ Not started |

### Learning log (Milestone 1)

Filled in as each piece lands — what was learned implementing it, in
Nadav's-future-interview terms.

- **`wire_checksum`** — first version had three bugs, each instructive:
  (1) XOR'd the loop index instead of `data[i]` — the checksum ignored
  the data entirely; (2) hand-copied FNV prime missing a digit — would
  have "worked" tx↔rx but failed the Milestone 4 PCAP cross-check,
  hence: always test hand-copied constants against known values;
  (3) `int` instead of `uint32_t` — the constant doesn't fit in signed
  int and the multiply overflows every iteration, which is *undefined
  behavior* for signed but defined wrap-around (mod 2³²) for unsigned;
  the algorithm depends on that wrap. Verified against known vectors
  (empty → 0x811C9DC5, "a" → 0xE40C292C, "ab" → 0x4D2505CA) under
  ASan/UBSan. Key mechanism: XOR injects each byte into the low bits;
  multiplying by the prime (2²⁴ + 0x193) copies them to the high byte
  and smears the middle — that chaining is what makes byte order
  matter, unlike a plain sum.
- **`wire_hdr_pack`** — done, via `put_be16/32/64` helpers (the
  professional idiom; BSD ships these as `be32enc` etc.). Verified by
  known-value test: poisoned buffer, all 24 bytes exact, error paths
  rejected, ASan/UBSan clean. Learned: (1) the "window at the bottom"
  model — cast to `uint8_t` keeps only the low byte, `>>` slides the
  wanted byte into that window, shifts are always multiples of 8
  (per-width: 24/16/8/0, 8/0, 56..0); (2) first attempt wrote one byte
  per field and used non-multiple-of-8 shifts — each `buf[i]` holds
  exactly one byte, an N-byte field needs N stores; (3) `buf[4]`
  (byte VALUE) vs `buf + 4` / `&buf[4]` (ADDRESS) — passed a value
  where a pointer was expected; compiles as a *warning* but crashes at
  runtime, hence zero-warnings discipline; (4) literal spelling ≠
  width: `0` passed as `uint16_t` writes two zero bytes — the type
  decides; leading-zero literals are octal (`010` == 8), so never
  zero-pad decimal constants; (5) pack is pure transcription — the
  checksum is computed by the caller over the payload, not by pack.
- **`wire_hdr_unpack`** — done, via `get_be16/32/64` getters (one arg —
  pointer to the field's first byte — returning the value; mirror of
  the putters). Round-trip test green: pack→unpack preserves every
  field, short-buffer/bad-magic/oversize-len all rejected, ASan/UBSan
  clean. Learned: (1) integer promotion — narrow types (uint8_t/16)
  are silently widened to *signed* int before any arithmetic, so
  `p[0] << 24` can shift into the sign bit (UB) and `<< 56` exceeds
  int width (UB); cast to the full-width unsigned target BEFORE
  shifting; (2) unpacking = lift each byte to its lane with `<<`,
  merge with `|` (OR merges disjoint lanes with no carries — states
  intent better than `+`); (3) compiler catches type-shaped bugs
  (wrong-width shifts, value-vs-pointer) but is silent on logic bugs
  (copy-pasted p[2]/p[3] indices, `return -1` on the success path) —
  those need tests; (4) getters always read from p[0] — the caller's
  `buf + offset` already positioned the pointer; (5) strict aliasing +
  alignment are why pointer-casting a byte buffer is UB and memcpy or
  shifts are the sanctioned forms; ntohl/htonl exist but have no
  standard 64-bit sibling.
- **`tx_run`** — _(pending)_
- **`rx_run`** — _(pending)_

### Concepts covered so far (explained, pre-implementation)

- **Packet / wire format:** networks deliver raw bytes; both sides must
  agree on an exact byte layout (the header) to interpret them.
- **Serialize/deserialize:** converting an in-memory struct to/from the
  agreed flat byte layout.
- **Endianness & network byte order:** CPUs store multi-byte numbers in
  different byte orders; the wire standard is big-endian. `htonl`/`ntohl`
  convert — or shift-and-mask packing sidesteps the issue entirely.
- **Struct-cast-onto-buffer trap:** casting a byte buffer to a struct
  pointer breaks due to compiler padding, byte order, and alignment
  (undefined behavior).
- **UDP vs TCP:** TCP repairs loss/reordering before the app sees it and
  self-throttles (congestion control); UDP shows the network as-is with
  1 datagram = 1 packet — which is what a measurement tool needs.
- **ASan/UBSan:** compiler instrumentation catching memory errors and
  undefined behavior at runtime; separate build because of ~2× overhead.
- **FNV-1a checksum:** XOR-then-multiply-by-prime hash for corruption
  detection; avalanche effect; non-cryptographic (catches accidents, not
  attackers); relies on well-defined *unsigned* overflow.

---

## Milestone 2 — Event-Driven Receiver (epoll) — not started

**Goal:** replace blocking I/O with a non-blocking epoll event loop;
live stats via timerfd.
**Exit criteria:** receiver sustains a high-rate flood without blocking,
reports packets/sec and sequence gaps live.
**Planned ownership:** 🧑 socket config (`O_NONBLOCK`, `SO_RCVBUF`,
`SO_REUSEPORT`), epoll loop, drain-until-EAGAIN · 🤖 stats printer,
architecture walkthroughs.

## Milestone 3 — Multithreaded Generator & Verification — not started

**Goal:** pthread worker senders; precise loss/reorder/duplicate
tracking on the receiver.
**Exit criteria:** clean under both `make tsan` and `make asan`; exact
loss/reorder/duplicate counts.
**Planned ownership:** 🧑 thread lifecycle, sequence-space partitioning,
shared stats (mutex/atomics), gap/reorder detection · 🤖 design
discussion, struct definitions, rate-limiting math.

## Milestone 4 — Python Test Harness — not started

**Goal:** automated end-to-end verification: orchestrate runs, parse
PCAPs with scapy, cross-check the C receiver's claims.
**Exit criteria:** `run_tests.py` green/red summary; induced
loss/reorder/duplication (tc netem) detected; a lying receiver caught
by the PCAP cross-check.
**Planned ownership:** 🤖 nearly all of it (argparse, subprocess
orchestration, scapy parsing — permitted boilerplate).
