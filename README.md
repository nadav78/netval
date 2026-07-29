# netval

High-throughput, low-level network verification and traffic generation
tool. A C engine sends sequenced UDP datagrams and verifies what
actually arrives — detecting packet loss, reordering, duplication, and
corruption — with a Python/scapy harness that cross-checks the engine's
claims against raw packet captures.

Built in pure C11 against Linux POSIX APIs (`socket`, `epoll`,
`pthread`) with zero external dependencies.

> **Status: in active development.** Milestone 1 (wire format codec)
> is complete; the UDP tx/rx engine is in progress. See
> [PROGRESS.md](PROGRESS.md) for the current state.

## Design

```
 ┌────────────┐   sequenced UDP datagrams    ┌────────────┐
 │  netval tx │ ───────────────────────────► │  netval rx │
 │ (generator)│      24-byte header +        │ (verifier) │
 └────────────┘      checksummed payload     └────────────┘
                                                   │
                              loss / reorder / corruption report
```

Every packet carries a custom big-endian wire header:

| offset | size | field         | purpose                        |
|-------:|-----:|---------------|--------------------------------|
| 0      | 4    | magic `NETV`  | reject foreign/garbage traffic |
| 4      | 4    | sequence      | detect loss & reordering       |
| 8      | 8    | timestamp ns  | latency measurement            |
| 16     | 2    | payload len   | framing validation             |
| 18     | 2    | reserved      | future use                     |
| 20     | 4    | FNV-1a checksum | payload corruption detection |

Serialization is explicit shift-and-mask packing — no struct casts, no
compiler-padding or endianness assumptions, UB-free under
`-fsanitize=address,undefined`.

## Building

Requires GCC/Clang and GNU Make on Linux.

```sh
make          # release build (-O2)          → build/release/netval
make debug    # debug build (-g -O0)         → build/debug/netval
make asan     # AddressSanitizer + UBSan     → build/asan/netval
make tsan     # ThreadSanitizer              → build/tsan/netval
```

## Usage

```sh
# terminal 1: receiver
./build/release/netval --mode rx --port 9000

# terminal 2: sender — 10k packets at 1000 pps
./build/release/netval --mode tx --dest 127.0.0.1 --port 9000 \
                       --count 10000 --rate 1000
```

Run `netval --help` for all options.

## Roadmap

**Core** — the complete project; portfolio-complete after milestone 4:

1. **Skeleton & wire format** — CLI, logging, header codec, blocking
   UDP tx+rx ✅
2. **Non-blocking `epoll` receiver** — readiness-driven event loop,
   drain-until-`EAGAIN`, live stats, clean shutdown
3. **Threading & precise sequence accounting** — spec-first
   loss/reorder/duplicate semantics with a bounded reordering window,
   per-flow `pthread` sender workers, TSan-clean
4. **Independent validation & fault injection** — a logically
   independent Python/scapy implementation cross-checks the C engine
   against raw packet captures, with deterministic and `tc netem`
   fault injection

**Optional extensions** — pursued after the core is done, never as
blockers:

5. **One measured optimization story** — baseline → evidence → one
   justified change (e.g. `recvmmsg` batching) → honest re-measurement
   in `BENCHMARKS.md`
6. **Multicore scaling** *(specialized)* — `SO_REUSEPORT` sharding and
   CPU pinning, only if measurement shows single-core rx saturated

## Project notes

This is a learning-focused systems project: the core C (wire codec,
socket loops, threading) is hand-written, with AI assistance
deliberately limited to boilerplate and code review — the rules are in
[CLAUDE.md](CLAUDE.md), and [PROGRESS.md](PROGRESS.md) keeps an honest
log of who built what and what was learned.

## License

[MIT](LICENSE)
