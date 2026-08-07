# netval — Progress & Learning Log

Living document tracking the milestone plan, who built each piece
(per the ownership rules in CLAUDE.md), and what Nadav learned along
the way. Claude updates this as work lands.

**Ownership legend:** 🧑 Nadav (core logic — interview material) · 🤖 Claude (permitted boilerplate) · 📋 Claude spec / Nadav implementation

---

## Milestone Overview

| # | Milestone                                  | Tier                    | Status         |
|---|--------------------------------------------|-------------------------|----------------|
| 1 | Skeleton & Wire Format                     | Required                | ✅ Complete    |
| 2 | Non-Blocking epoll Receiver                | Required                | ⬜ Not started |
| 3 | Threading & Precise Sequence Accounting    | Required                | ⬜ Not started |
| 4 | Independent Validation & Fault Injection   | Required                | ⬜ Not started |
| 5 | One Measured Optimization Story            | Optional — perf story   | ⬜ Not started |
| 6 | Multicore Scaling                          | Optional — specialized  | ⬜ Not started |

Milestones 1–4 build the *correct, verified tool* and are the complete
project: **netval is portfolio-ready after Milestone 4** (see the
Portfolio-Ready Checklist below). Milestones 5–6 are the
performance-engineering arc (scope chosen 2026-07-28: perf depth over
protocol breadth) — valuable extensions, never blockers. They must not
delay applications or complicate the earlier milestones.

---

## CURRENT FOCUS — Interview Track (started 2026-08-04)

Re-sequenced subset of M2–M4 targeting an interview in 1–2 weeks:
lite versions that make each resume bullet concretely demonstrable,
with the heavyweight milestone artifacts explicitly deferred (not
dropped). Milestone definitions below are unchanged; work done here
counts toward them.

| Phase | Scope | Backs | Status |
|-------|-------|-------|--------|
| A | epoll receiver: `O_NONBLOCK`, drain-until-`EAGAIN`, `--idle-timeout`, LT/ET decision, strace evidence | epoll bullet | ✅ Done |
| B | tx `--threads N` workers; rx malloc'd per-flow table (src ip:port), dup/backward-seq counter, TSan-clean | pthreads + memory-mgmt bullets | 🔨 tx + rx table ✅; TSan run pending |
| C | netem (probe; proxy fallback) fault injection; GDB-first debugging stories; disassembly walkthroughs | fault-injection + GDB bullet | ⬜ |

Each phase ends with a GDB walkthrough of the new code (structured,
with short assembly TLDRs). Deferred until after the interview:
accounting spec + `account.{h,c}`, wire-format freeze review, Python/
scapy harness, CI, write-ups, M5/M6.

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
| `src/tx.c` — blocking UDP sender         | 🧑    | ✅ Done |
| `src/rx.c` — blocking UDP receiver       | 🧑    | ✅ Done |
| rx summary formatter                     | 🤖    | ✅ Done |
| End-to-end localhost run, ASan clean     | 🧑    | ✅ Done |

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
- **`tx_run`** — done: socket() → sockaddr_in (zeroed, htons port,
  inet_pton) → per-packet assemble (header fields, seq+i payload
  pattern, checksum, pack) → sendto → nanosleep pacing. Verified live:
  Python listener received exact expected bytes ("NETV" magic, BE seq
  0/1/2, ticking timestamps, correct FNV-1a independently recomputed
  in Python); 1000-packet max-payload flood ASan/UBSan clean.
  Learned: (1) errno is only meaningful after a call REPORTS failure —
  checking sendto's return against the wrong constant produced
  "error: Success"; idiom is `== -1`; (2) buffer capacity
  (NETVAL_MAX_DGRAM) vs actual packet size (HDR+payload_len) — send
  the letter, not the envelope box; (3) loop-variable width: uint8_t i
  vs payload_len up to 1200 = infinite wrap loop (defined unsigned
  wrap, logic bug not UB); (4) format specifiers match per-argument
  (%s↔char*, %u↔uint32_t) — printf trusts the format blindly;
  (5) "implicit declaration of X" ⇒ missing header, and one missing
  header cascades into dozens of errors — always fix the FIRST error;
  (6) &header for struct-to-pointer args (structs don't decay; arrays
  do); (7) widen operands not results: (uint64_t)tv_sec * 1e9ULL;
  (8) `= {0}` any struct handed to the kernel; C never zeroes locals;
  (9) IDE autocomplete can insert C++/internal headers (<cerrno>,
  <bits/time.h>) — compiler is ground truth, but this time the
  squiggle was real.
- **`rx_run`** — done: socket() → bind(INADDR_ANY) → sigaction(SIGINT)
  → blocking loop (recvfrom with MSG_TRUNC → classify → count) →
  break on stop or error → close + summary. Verified end-to-end:
  1000 packets tx→rx on loopback, 1000 valid, 0 rejects, exit 0;
  1200-byte max-payload run ASan/UBSan clean; a hand-crafted Python
  reject suite exercised every class (short buffer, bad magic, length
  mismatch, flipped checksum byte, 2024-byte oversized, seq jump)
  with the four counters mutually exclusive and summing to observed.
  Learned: (1) **trust boundaries** — the only untrusted input is the
  packet bytes; validate those hard, `assert()` internal contracts,
  and always check syscall returns (three different tiers, not one
  habit); (2) passing `sizeof(buf)` where `n` belongs disables a
  callee's own bounds check — a range check against a constant is not
  a consistency check against what actually arrived; (3) `sigaction`
  with `sa_flags = 0` is what produces `EINTR`; `signal()` under
  glibc supplies BSD semantics (`SA_RESTART`) and would silently
  swallow Ctrl-C; (4) a handler may only set a `volatile
  sig_atomic_t` flag — `printf`/`malloc`/`exit` are not
  async-signal-safe, and the counters are locals the handler can't
  reach anyway; (5) unbalanced braces always report at *end of file*,
  never at the broken line — misleading indentation is what hid it,
  and the compiler stayed silent about uninitialized counters only
  because the post-loop code was unreachable dead code it deleted;
  (6) a reject is a counted `continue`, never a `return` — one
  malformed datagram must not be able to kill the receiver;
  (7) libraries return, applications exit: `rc` propagates to
  `main`, and Ctrl-C is *success* (0) because it is the documented
  normal stop; (8) `uint64_t` needs `PRIu64`, not `%lu` — the type is
  `unsigned long` on 64-bit and `unsigned long long` on 32-bit.
- **`rx_run` epoll conversion (interview track A)** — done: fcntl
  O_NONBLOCK (read-modify-write to preserve flags) → epoll_create1 →
  epoll_ctl(ADD, EPOLLIN) → nested loops: outer = one lap per wakeup
  (epoll_wait, the ONLY sleep), inner = one lap per datagram (drain to
  EAGAIN); --idle-timeout via epoll_wait's ms timeout arg. Verified:
  5000/5000 at unlimited rate; idle-timeout, Ctrl-C, and error exits
  all reach the summary with the right reason; ASan/UBSan clean at max
  payload; strace: 236 sleep/wake cycles (M1) → 2 epoll_waits total,
  one wakeup draining 221 datagrams to EAGAIN, idle wait blocked at 0%
  CPU. Learned: (1) the **syscall error protocol is two channels** —
  return value says THAT it failed (== -1), errno says WHY; wrote
  `nready == -1 + EINTR` (arithmetic! == 3) which silently swallowed
  Ctrl-C: unkillable receiver, stop flag set but unreachable; (2) an
  `else` binds to the if above it, not to "errors" — the real-error
  branch belongs nested inside the -1 case, or the success path lands
  in it; (3) on a non-blocking fd, -1/EAGAIN is the *expected* end of
  every drain — the empty-check IS the failed read, there is no "is it
  empty?" syscall; (4) blocking = kernel picks when you wake;
  nonblocking+epoll = you pick where you sleep — the one-sleep-point
  model is what makes the timeout and (later) multiple fds possible;
  (5) epoll doesn't batch — your processing time does: it wakes on the
  first datagram, the pile forms while you drain, so amortization
  self-scales with load; (6) break exits the innermost loop only —
  fatal paths from the inner loop need goto done (and the goto-cleanup
  idiom is the C answer to "no destructors"); (7) epoll cut wakeups,
  not syscalls-per-datagram — recvmmsg (M5) is the syscall-count
  story; keeping the claims separate is what makes both defensible.
- **GDB walkthrough #1 (interview track A)** — done, on the live epoll
  receiver: breakpoint in the loop, `print nready`/`events[0]`
  (bitmask: 1 == EPOLLIN), hex-dumped a received datagram (`x/24xb`)
  and verified magic + big-endian seq against wire.h by eye; word-view
  (`x/1xw`) showed the same bytes swapped — little-endian display vs
  wire order in one command; `bt` through glibc's epoll_wait frame;
  caught the drain's EAGAIN endpoint (n=-1, errno=11) with a
  conditional breakpoint after a line breakpoint on a bare `break;`
  slid to an instruction the EAGAIN path bypasses. Bonus finds:
  "No symbol in current context" = block scope, live; GDB's Ctrl-C
  pauses the inferior without running its SIGINT handler; a restarted
  tx looks like loss/reorder to the single global expected_next —
  the per-flow motivation (Phase B), observed first-hand.
- **tx `--threads N` workers (interview track B, tx half)** — done:
  per-worker tx_worker struct (args in / results out through the one
  void* a pthread start routine gets), spawn+join in tx_run, one
  socket/flow/seq-space per worker, atomic_bool stop. Verified: 4×100k
  Ctrl-C mid-flight → 2234/2234/2234/2235 partial counts, clean
  summary, exit 0; 4×500 against rx = the fake-loss WARN storm (one
  global expected_next sees 4 interleaved flows as ~75% loss) — the
  per-flow table's before-picture. Learned: (1) struct assignment
  COPIES — `tx_worker w = workers[i]` initializes a throwaway copy;
  arrays decay, structs copy, same `=`; (2) fill the element, THEN
  spawn — after pthread_create returns the worker may already be
  reading it; (3) pthread_* return error numbers directly:
  strerror(ret), never errno; (4) join everything you spawned before
  returning — an early return pops the stack frame every worker's arg
  pointer targets (join bound = nspawned, worker failure deferred to
  rc, never short-circuits); (5) volatile sig_atomic_t guards
  same-thread signal interruption only; cross-THREAD visibility needs
  C11 atomics — lock-free atomic_bool is async-signal-safe too, one
  flag covering both hazards; (6) a process-directed signal is
  delivered to exactly ONE thread (here: main, in pthread_join) — the
  workers never saw EINTR and learned of the stop purely through the
  flag, which is why handler-sets-flag scales to threads; (7) uniform
  partial counts (±1) across workers = the atomic's visibility
  working, observable in the logs.
- **rx per-flow table (interview track B, rx half)** — done: flow_t
  records keyed on (src ip, src port) captured from recvfrom, calloc'd
  on first sight into a fixed 64-slot pointer array, linear-scan
  lookup, freed at teardown. Per-flow expected_next/received/gaps/
  backward; global reject classes unchanged. Verified: 4 flows x 500 =
  4 clean streams, next 500 each, zero gaps, zero WARNs (vs the
  pre-table storm where one global counter read 4 interleaved flows as
  ~75% loss); ASan clean incl. leak check at 8 flows x 2000 x 1200B,
  where genuine socket-buffer loss shows up honestly as per-flow gaps.
  Learned: (1) **use-after-free is silent** — freed the flow records
  before the summary read them; no crash, just wrong values. The
  corruption pattern was the diagnosis: first 16 bytes of each record
  garbage (glibc writes tcache next+key there), later fields perfect,
  and expected_next *identical across two independent flows* because a
  per-thread allocator key is shared. ASan named read/free/alloc lines
  in one run; my first theory (format string) was wrong and a
  one-field-per-call probe refuted it cheaply. (2) **goto-cleanup means
  everything between the loop and the label is dead code** — moved the
  summary above `done:` and it silently never ran (second time this
  file has produced unreachable post-loop code; in M1 it also hid an
  uninitialized-counter warning because GCC deleted the block).
  Correct order: label -> close fds -> READ state -> free state ->
  return. (3) calloc over malloc when most fields start at zero — and
  calloc checks count*size for overflow, which malloc(n*sz) does not.
  (4) reject before you allocate: the table-full check must precede
  calloc or a full table leaks a record per packet. (5) validate
  before you account — the flow lookup goes after the checksum so
  garbage packets can't create table entries (a spoofed-port flood
  would otherwise fill all 64 slots). (6) keys stay in network byte
  order because they're only ever compared; the summary's inet_ntop +
  ntohs is the single conversion point. (7) `f->backward;` compiled
  fine as a statement with no effect (-Wunused-value caught it), while
  never assigning expected_next at all was invisible to the compiler —
  type-shaped bugs get diagnosed, logic-shaped bugs need tests.

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

## Milestone 2 — Non-Blocking epoll Receiver — not started

**Learning objective:** readiness-based I/O. What "readiness" means,
why blocking I/O can't serve a flood, level-triggered vs edge-triggered
semantics, drain-until-`EAGAIN`, and correct handling of
`EAGAIN`/`EWOULDBLOCK`/`EINTR`.
**Portfolio value:** the epoll event loop is the single most
interview-visible Linux server pattern; being able to walk the control
flow line by line and defend the LT/ET choice is the point.
**Builds on:** M1's blocking rx is the "before" picture; the codec and
validation path are reused unchanged.

| Piece                                          | Owner | Status |
|------------------------------------------------|-------|--------|
| `O_NONBLOCK` via `fcntl` (preserve flags)      | 🧑    | ✅ Done (interview track A; `SO_RCVBUF` still ⬜) |
| epoll loop + drain-until-EAGAIN                | 🧑    | ✅ Done (interview track A) |
| timerfd live stats (same epoll set)            | 🧑    | ⬜ |
| SIGINT/SIGTERM clean shutdown (flag + EINTR)   | 🧑    | ✅ SIGINT (M1 + epoll_wait EINTR); SIGTERM ⬜ |
| `--idle-timeout` stop (M1's deferred "proper stop") | 🧑 | ✅ Done (interview track A; CLI flag 🤖) |
| LT-vs-ET decision brief (Nadav decides + ~½ page justification) | 📋 | ✅ Decided: LT — drain-to-EAGAIN makes the modes equivalent when correct; a drain regression under LT costs wakeups (loud), under ET strands data silently — wrong failure shape for a counting tool; ET's payoff needs many fds or a shared epoll instance, absent here |
| Stats printer formatting                       | 🤖    | ✅ Done (summary + stop-reason line) |
| Formalize M1 ad-hoc wire tests → `tests/` + `make test` | 🤖 | ⬜ |
| Burst-test script                              | 🤖    | ⬜ (strace_rx.sh covers the syscall-evidence part) |

**Done when — all of:**

1. **Stability test:** a 60-second maximum-rate run (tx unlimited rate)
   completes with no crash, no hang, no busy loop (blocked `epoll_wait`
   when idle, verified via `strace` / CPU%), and no leak (ASan).
2. **Counters are internally consistent and clearly defined:**
   received / malformed / corrupt are mutually exclusive classes — a
   datagram counts in exactly one; "datagrams observed" is their sum,
   not a separate tally. The final report compares transmitter sends,
   receiver datagrams observed, application rejects, and known
   socket-level drops (`/proc/net/udp`, `ss -u`) — documenting that
   these may not reconcile exactly: a successful `sendto()` only proves
   the kernel accepted the datagram, and UDP loss can occur outside the
   receiver's socket queue. Drops are permitted; silent miscounting or
   double-counting is not.
3. Live pps / bytes / malformed / corrupt stats tick via timerfd.
4. Ctrl-C and `--idle-timeout` both produce a clean summary.
5. Malformed/truncated packets flow through the event loop as counted
   rejects — no crash, ASan clean.
6. A captured `strace` transcript shows (a) multiple receive calls per
   `epoll_wait` wakeup under burst and (b) blocked `epoll_wait` when
   idle.
7. Written answers (learning log) to: what readiness means; why
   non-blocking mode is necessary; why one readiness event may cover
   many datagrams; how LT and ET differ; why the loop must normally
   drain until `EAGAIN`; what could cause a busy loop or missed work.
8. `make test` green.

**Required tests:** golden wire vectors + round-trip + boundary /
truncated / bad-magic (formalized from M1), burst flood test,
malformed-packets-mixed-into-flood test, idle-timeout test.
**Tools:** `strace` (syscall-pattern evidence), `tcpdump`/Wireshark on
`lo` when packet-level questions arise, ASan/UBSan.
**Likely traps:** LT without draining "works" but hides the bug ET
would expose; integer promotion in event-mask checks; printing from a
signal handler (async-signal-safety); assuming one epoll event = one
datagram; forgetting `epoll_wait` returns early with `EINTR`; `errno`
only meaningful after a call reports failure.
**Skip (poor return):** a generic event-loop/reactor abstraction or
callback framework — direct, inspectable control flow beats
"extensible"; epoll on the tx side; multiple sockets; `SO_REUSEPORT`
(that's M6's concept — not needed yet); signalfd (sigaction suffices;
worth a one-line aside at most).

## Milestone 3 — Threading & Precise Sequence Accounting — not started

**Learning objective:** (a) defining and implementing defensible
loss/reorder/duplicate semantics for a passive receiver — the project's
central technical feature, not a side counter; (b) pthread lifecycle
and data-race-free shared state with an honest architectural reason for
concurrency.
**Portfolio value:** a written accounting spec + a deterministic,
unit-tested classifier + TSan-clean concurrency is exactly the
"can reason about correctness" evidence junior systems candidates
rarely show.
**Builds on:** M2's epoll loop is the natural host for the accounting
module; M1's `seq` field finally gets real semantics.

### Track 0 — wire-format freeze review (first, one-time)

(🧑 decision, 📋 brief.) Before the format freezes for M4's independent
harness: evaluate whether the checksum should cover the immutable
header fields + payload (checksum field zeroed during computation)
instead of payload-only. Payload-only means a corrupted `seq`/`ts_ns`
passes validation and gets *misclassified* by the accounting — keeping
it must be an intentional, documented decision with that impact stated
in the spec. The same review decides whether `reserved` becomes an
explicit `stream_id` for the flow model below, or flows stay identified
by source address. One review, decided once, format frozen after.

### Track A — sequence accounting (the centerpiece)

**Spec before code:** `docs/sequence-accounting.md` (📋 — Claude drafts
the framework, questions, and test sequences; Nadav writes the semantic
decisions). It must define: in-order; forward gap; suspected lost;
confirmed lost (at reconciliation); late arrival (within retained
window); reordered; duplicate; `out_of_window` (behind retained
history); malformed; checksum failure; sequence wraparound; end-of-run
reconciliation. It must include a worked classification table for:

```text
100, 101, 102
100, 102, 101
100, 102, 102, 101
100, 103, 101, 102
100, 103, 101
MAX-1, MAX, 0, 1
```

and an explicit statement of what a passive receiver *cannot* know: a
missing sequence number is only "suspected lost" until the observation
window closes over it, and an arrival from behind the retained window
cannot be distinguished as late-first-arrival vs duplicate — it is
classified honestly as `out_of_window`, and exact loss becomes a lower
bound, not a certainty.

**Recommended design** (decided in the spec): a bounded reordering
window — power-of-two ring bitmap over recent sequence numbers
(default ~1024) **per flow**. Arrivals ahead advance the window,
converting evicted unset slots to suspected-lost; within the window,
bit set → duplicate, bit unset → fill (reordered if below max-seen);
behind the window → `out_of_window`. Wraparound handled with
serial-number arithmetic (RFC 1982-style unsigned subtraction — Core
Skill 1 applied for real). End-of-run reconciliation sweeps remaining
suspected → confirmed lost. The spec should discuss and reject the
alternatives: retaining full history (unbounded memory for unbounded
runs) and pure end-of-run reconciliation (no live stats).

**Structure:** a pure module `src/account.{h,c}` — feed a sequence
number, get a classification; no sockets — so it is deterministically
unit-testable. (🧑 module, 📋 header skeleton.)

### Track B — concurrency with a real reason

tx gains `--threads N` worker senders: a single sender thread caps
offered load, and M5/M6 measurement needs to overdrive the receiver.
rx **stays single-threaded** — the accounting runs in the epoll loop;
that decision and its rationale are documented (no threads for
threads' sake).

**Flow model — one flow per worker, not disjoint ranges:** each worker
is an independent flow with its **own socket** (own source port) and
its **own sequence space starting at 0**. The receiver keeps a small
per-flow table (keyed by source ip:port from `recvfrom`, or by
`stream_id` if Track 0 chose that) and runs one accounting-window
instance per flow. The spec must record the rejected alternative:
disjoint global ranges interleaved into one accounting stream would
make normal thread interleaving look like extreme loss/reordering.
Bonus, noted now: per-flow design is exactly what M6 needs — distinct
4-tuples are what `SO_REUSEPORT` hashes across sockets, and per-flow
state gives each future rx thread ownership of its flows with no
sharing.

Concurrency skills taught: `pthread_create`/`pthread_join`; per-worker
ownership of socket + sequence space + stats struct (aggregated at
join — minimal synchronization *by design*); one shared atomic stop
flag; error propagation via per-thread status; per-worker rate
limiting. A mutex only where state is actually shared (e.g. the live
progress line) — and the synchronization rules written down as a
"who owns what" table.

| Piece                                            | Owner | Status |
|--------------------------------------------------|-------|--------|
| Wire-format freeze review (checksum coverage, stream_id) | 📋+🧑 | ⬜ |
| `docs/sequence-accounting.md` spec               | 📋    | ⬜ |
| `src/account.{h,c}` classifier                   | 🧑    | ⬜ |
| Per-flow table in rx                              | 🧑    | ✅ Done (interview track B) |
| tx `--threads N` workers (sockets, lifecycle, stop, errors) | 🧑 | ✅ Done (interview track B) |
| Per-worker rate limiting                          | 🧑    | ⬜ |
| Deterministic account-module test harness         | 🤖    | ⬜ |
| Stress/interleave test scripts                    | 🤖    | ⬜ |
| Write-up #2: the accounting model                 | 🧑+🤖 | ⬜ |

**Done when — all of:** freeze review decided and documented; spec
complete including the worked sequences; `account.c` passes
deterministic unit tests for every spec sequence including wraparound
and out-of-window arrivals; multi-threaded tx flood + rx accounting
runs TSan-clean AND ASan-clean; stress test (4+ workers, unlimited
rate, ≥1M packets, 60s+) clean with internally consistent counters;
end-of-run report distinguishes all classes **per flow** plus an
aggregate; synchronization rules documented; write-up #2 drafted;
learning-log entries added.

**Required tests:** deterministic account-module tests (all spec
sequences + edges + wraparound + out-of-window); a multi-flow
interleave test (N flows interleaved → each accounts cleanly, no
cross-flow false loss); TSan stress run with genuine contention;
synthetic duplicate/late/reorder feeds.
**Tools:** TSan (on genuinely contended runs — a clean TSan pass on an
uncontended path proves nothing), GDB (`info threads`, per-thread
backtraces), ASan.
**Likely traps:** counting a late arrival twice (as lost *and*
received); off-by-one on window advance; naive `a < b` sequence
comparison breaking at wraparound (use the signed difference of an
unsigned subtraction); `sig_atomic_t` vs C11 atomics confusion;
sharing `cfg` across threads (fine — read-only after spawn — but say
so in the ownership table).
**Skip (poor return):** lock-free structures, condition-variable
pipelines, thread pools, rx-side worker threads, false-sharing padding
(deferred until it can be *measured*, in M6).

## Milestone 4 — Independent Validation & Fault Injection — not started

**Learning objective:** proving a C implementation correct from the
outside — independent re-implementation, packet-capture ground truth,
and controlled fault injection with honest assertion discipline.
**Portfolio value:** this is the project's correctness differentiator.
"My Python harness would catch my C engine lying" is a claim almost no
junior portfolio can make.
**Builds on:** M3's spec is the harness's oracle; the impairment proxy
exercises exactly the semantics the spec defined.

**Independence rules (non-negotiable):** the Python codec
(`harness/netval_proto.py`) is written from the wire *spec* (the
byte-layout table), never by translating `wire.c`. FNV-1a is
re-implemented from the published algorithm and checked against
**public** FNV test vectors. A hand-written golden-vector file
(`tests/golden_vectors.txt`) is consumed by BOTH the C and Python test
suites — vectors are spec, never generated output of either
implementation (shared generated constants would let both sides share
the same mistake unnoticed).

| Piece                                              | Owner | Status |
|----------------------------------------------------|-------|--------|
| Independent Python encoder/decoder + FNV-1a        | 🤖    | ⬜ |
| Scapy PCAP parsing                                 | 🤖    | ⬜ |
| `run_tests.py` orchestration (green/red summary)   | 🤖    | ⬜ |
| Random valid + malformed packet generators         | 🤖    | ⬜ |
| Deterministic UDP impairment proxy (`harness/impair_proxy.py`) | 🤖 | ⬜ |
| `tc netem` wrapper (context manager, guaranteed cleanup) | 🤖 | ⬜ |
| Lying-receiver mutation check                      | 🤖    | ⬜ |
| CI workflow (see Build & CI below)                 | 🤖    | ⬜ |
| `scripts/demo.sh` + README finalization (limitations, disclosure) | 🤖 | ⬜ |
| Review of every assertion; any C-side fixes exposed | 🧑   | ⬜ |

The deterministic proxy forwards tx→rx while injecting *exact*
scenarios on command — drop seq 5, swap 7 and 8, duplicate 9 — so the
M3 spec's classification tables can be asserted end-to-end through
real sockets. netem provides realistic *statistical* impairment on top.

**Assertion discipline:**

- *Exact:* C rx report vs scapy-derived PCAP ground truth;
  proxy-injected scenarios vs the M3 spec tables; cross-decode (C tx
  bytes → Python decoder, Python-crafted packets → C rx); round-trip
  properties (`decode(encode(p)) == p`) in both languages.
- *Statistical / within tolerance:* everything netem produces (loss %,
  reorder %, duplication %, corruption %) — asserted over large N with
  tolerance, never exactly; netem's nominal rates are targets, not
  guarantees.
- Golden vectors + explicit boundary cases remain primary; randomized
  testing supplements them, never replaces them.

**Done when — all of:** `run_tests.py` fully green; every M3 spec
sequence validated end-to-end through real sockets via the proxy;
netem suite passes within tolerance (or skips cleanly with a
documented reason — see traps); a deliberately mutated ("lying")
receiver is caught by the PCAP cross-check; a random-malformed flood
into rx is crash-free under ASan; CI live; README finalized with
limitations, demo, and AI disclosure; **the Portfolio-Ready Checklist
below is fully checked → start applying.**

**Tools:** scapy, `tcpdump`, `tc`/netem, GitHub Actions.
**Likely traps (WSL2 ones matter here):** the stock WSL2 kernel often
lacks the `sch_netem` module — the harness must probe
(`tc qdisc add dev lo root netem ...`) at startup and skip-with-warning
rather than fail, with the deterministic proxy carrying exact-semantics
coverage regardless; netem on `lo` shapes *both* directions unless
filtered; `tc` needs sudo — document it, and never leave the qdisc
behind (cleanup in a `finally`); tcpdump on `lo` may see packets before
netem's impairment depending on attach point; loopback checksum
offload can confuse capture-level checks.
**Skip (poor return):** translating the C codec into Python "to be
safe" (defeats the whole point); a pytest plugin architecture;
per-test docker/namespace isolation (a plain netns is optional
stretch, not required); fuzzing frameworks (the random-malformed
generator is enough at this scope).

---

## Portfolio-Ready Checklist (after Milestone 4)

- [ ] Core C is clean and understandable — Nadav can explain every line
- [ ] Binary protocol documented (byte-layout table matches the code)
- [ ] Blocking (M1) and non-blocking (M2) networking both correct
- [ ] Sequence-accounting semantics precise: spec and implementation
      agree, including wraparound and out-of-window behavior
- [ ] Builds warning-clean with `-Wall -Wextra -Werror` on GCC and Clang
- [ ] ASan/UBSan-clean execution
- [ ] TSan-clean concurrent execution under genuine contention
- [ ] Deterministic unit + boundary tests (`make test`)
- [ ] Independent Python/scapy cross-validation green
- [ ] Automated fault-injection tests green (proxy exact; netem
      statistical or documented-skip)
- [ ] Reproducible demo: `./scripts/demo.sh`
- [ ] Architecture + protocol documentation in the README
- [ ] At least one substantial technical write-up published
- [ ] Honest limitations section
- [ ] No major undocumented correctness assumptions

Milestones 5–6 proceed in parallel if desired — never as blockers.

---

## Milestone 5 — One Measured Optimization Story — OPTIONAL

**Learning objective:** defensible benchmarking methodology — baseline,
evidence, one justified change, honest re-measurement. Not an
optimization checklist.
**Portfolio value:** one rigorous before/after story with methodology
beats ten claimed optimizations; a documented *negative* result (e.g.
"a bigger `SO_RCVBUF` changed nothing, and here's why") is itself
strong interview material.
**Builds on:** M4's harness generates and verifies the workload; M3's
multi-threaded tx provides the offered load.

**The one question:** *does batching receives with `recvmmsg` raise
sustainable single-core rx throughput — and why?* (`sendmmsg` and
`SO_RCVBUF` sweeps are secondary, pursued only if the evidence points
there; `strace -c` syscall-rate evidence is what justifies picking
batching in the first place.)

**Fixed process:** 1 define the workload → 2 record the environment →
3 measure the baseline → 4 identify the bottleneck from evidence →
5 select one justified optimization → 6 implement it (🧑, behind
`--batch N`) → 7 re-measure → 8 explain the result and tradeoffs.

`BENCHMARKS.md` records: CPU, architecture, kernel, compiler + flags,
packet size, loopback caveat, socket-buffer settings, run count,
warmup procedure, median + spread, CPU utilization, syscall rate.
Negative or null results are first-class outcomes — documented, not
hidden.

**Done when (minimum for real value):** a recorded baseline + one
implemented change + an honest before/after with methodology in
`BENCHMARKS.md` — even if the delta is ~0.

**Tools:** `perf stat` (WSL2 usually lacks hardware PMU counters —
software events only; say so in the methodology), `strace -c`,
`/usr/bin/time`, `/proc/net/udp` + `ss -u` to locate drops (every drop
accounted for).
**Likely traps:** measuring a tx-limited system and attributing the
ceiling to rx — first prove the multi-threaded sender can overdrive
the receiver; loopback ≠ real network (state it); drops during a run
silently invalidating the pps number; no warmup; single-run numbers;
WSL2 virtualization noise.
**Skip (poor return):** implementing the whole optimization menu;
io_uring; zero-copy; `SO_BUSY_POLL`; kernel bypass — one line
acknowledging they exist is plenty.

## Milestone 6 — Multicore Scaling — OPTIONAL, SPECIALIZED

For high-performance networking / infrastructure / kernel-adjacent
roles specifically. **Gate: only start if M5's evidence shows
single-core rx genuinely saturated** — otherwise there is nothing
honest to scale and this milestone is dropped without regret.

**The exact question:** with single-core rx saturated, does
`SO_REUSEPORT` sharding across N pinned rx threads
(`pthread_setaffinity_np`) scale received pps ≈ linearly to N=4?
**Expected bottleneck:** rx-thread CPU. **Success bar (stated up
front):** e.g. ≥1.7× at 2 cores — and an honest "it didn't scale,
here's why" report also counts as completion.

**Methodology:** same discipline as M5, extended per-thread; per-flow
stats stay thread-owned (the M3 flow model gives each rx thread
ownership of its flows with no sharing); this is where false-sharing /
cache-line layout finally becomes a *measured* topic rather than a
premature one.

**Likely traps / environmental limits:** `SO_REUSEPORT` balances by
4-tuple flow hash — a single tx flow lands on ONE socket forever; the
M3 design saves this (each tx worker has its own source port → its own
flow), and load spread must be verified, not assumed. Loopback tx may
become the bottleneck before rx scales. WSL2 vCPU scheduling can fake
or hide scaling — results must carry that caveat.
**Skip (poor return):** NUMA, XDP/AF_XDP, DPDK, io_uring — a
"future directions" one-liner only.

---

## Cross-Cutting Plans

### Continuous correctness testing (grows with each milestone)

| Test class                                   | Lands |
|----------------------------------------------|-------|
| Golden wire vectors, round-trip, boundaries, truncation, bad magic | M2 (formalized from M1 ad-hoc) |
| Burst traffic, malformed-in-flood, idle timeout | M2 |
| Sequence classification (deterministic), wraparound, out-of-window, multi-flow interleave | M3 |
| Concurrency stress under TSan                | M3 |
| Cross-language validation, random valid/malformed generation, fault-injected end-to-end | M4 |

### Debugging & observability discipline

Tools answer *specific engineering questions*, not collect badges.
Meaningful investigations get a learning-log entry in the form:
**question → tool → evidence → root cause → what changed** (this
extends the existing M1 learning-log habit).

### Build & CI (lands at M4)

Local workflow: `make && make test && make asan && make tsan &&
./scripts/demo.sh`. CI (GitHub Actions): GCC + Clang builds with
`-Wall -Wextra -Werror`, `make test`, an ASan/UBSan job, a TSan job,
and a Python-harness smoke job (netem-free path). Nothing fancier —
build infrastructure must not distract from the systems work.

### Documentation plan

- README: understandable in ~60 seconds — packet-layout table (done),
  example commands, example normal output, a controlled impairment
  demo, architecture overview, verification summary, limitations.
- Exactly **two** technical write-ups (no shallow doc sprawl):
  1. How the epoll receiver drains readiness events (M2).
  2. How loss, reordering, duplication, late arrival, and
     out-of-window are classified (M3).
- `BENCHMARKS.md` if and only if M5 happens.

### Limitations (README section, written at M4)

UDP remains unreliable; missing packets can be dropped at multiple
layers the receiver can't see; sequence accounting uses a bounded
observation window (`out_of_window` arrivals make exact loss a lower
bound); loopback performance does not represent a real network;
one-way latency across hosts requires synchronized clocks; FNV-1a is
integrity-checking for controlled testing, not cryptographic
authentication; Linux-specific APIs limit portability; receiver
statistics cannot always prove the *cause* of a missing packet.

### Checksum position

Keep FNV-1a — no switch to CRC32C for optics. Its coverage
(header-minus-checksum + payload vs payload-only) is decided once in
the M3 Track-0 freeze review and not revisited. Document honestly:
exactly which bytes it covers, what changes it detects, why it's
useful in controlled testing, why it isn't cryptographically secure,
and why it doesn't replace transport-/link-layer integrity.

### Portability (optional, post-M4)

Run build + tests on ARM64 Linux — easiest via a GitHub Actions
`ubuntu-24.04-arm` job (free for public repos). Goal: expose
alignment, integer-width, byte-order, and compiler-behavior
assumptions. Two architectures ≠ proven portability — say so.

### External validation (post-M4)

Have another developer follow the README cold; get one focused C /
systems code review; run sender and receiver on two physical machines;
publish one concise technical write-up; resolve any externally
reported issue. GitHub stars are not a metric.

### AI disclosure

Public statement (README) stays short and professional:

> I implemented the wire codec, socket operations, event loop,
> sequence accounting, synchronization, and performance-critical C
> code. AI assistance was used for selected scaffolding, test
> automation, documentation review, and systems mentoring.

Detailed ownership history lives here in PROGRESS.md.
