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

1. **Skeleton & wire format** — CLI, logging, header codec ✅ / blocking
   UDP tx+rx 🔨
2. **Event-driven receiver** — non-blocking sockets, `epoll` loop, live
   packets/sec and gap reporting
3. **Multithreaded generator & verification** — `pthread` worker
   senders, precise loss/reorder/duplicate accounting, TSan-clean
4. **Python test harness** — subprocess orchestration, scapy PCAP
   cross-validation, fault injection via `tc netem`
5. **Syscall batching & socket tuning** — measured baseline, then
   `sendmmsg`/`recvmmsg` and buffer tuning for single-core throughput
6. **Multi-core scaling & benchmark report** — `SO_REUSEPORT`
   sharding, CPU pinning, and a published `BENCHMARKS.md` with the
   full baseline→optimized progression

## Project notes

This is a learning-focused systems project: the core C (wire codec,
socket loops, threading) is hand-written, with AI assistance
deliberately limited to boilerplate and code review — the rules are in
[CLAUDE.md](CLAUDE.md), and [PROGRESS.md](PROGRESS.md) keeps an honest
log of who built what and what was learned.

## License

[MIT](LICENSE)
