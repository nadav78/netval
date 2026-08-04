#!/usr/bin/env bash
# strace_rx.sh — capture the receiver's syscall pattern under a burst.
# (boilerplate, Claude-generated)
#
# Usage: scripts/strace_rx.sh [output-file] [port] [count]
#
# Runs rx under strace tracing only the interesting calls, fires a tx
# burst at it, lets it sit idle for a moment (to show the blocked wait),
# then stops it with SIGINT so the summary still prints.
#
# What to look for in the transcript:
#   M1 (blocking):  one recvfrom() per datagram, each returning a length;
#                   when idle, a recvfrom() with no return yet = blocked.
#   M2 (epoll):     epoll_wait() returning once, then MANY recvfrom()
#                   calls until one returns -1 EAGAIN; when idle, an
#                   epoll_wait() with no return yet = blocked, 0% CPU.
set -euo pipefail

OUT="${1:-/tmp/netval_strace_rx.txt}"
PORT="${2:-9200}"
COUNT="${3:-2000}"
BIN="$(dirname "$0")/../build/release/netval"

[ -x "$BIN" ] || { echo "build first: make" >&2; exit 1; }
command -v strace >/dev/null || { echo "strace not installed" >&2; exit 1; }

# -tt  wall-clock timestamps    -T  time spent inside each syscall
# -e   only the calls that tell the readiness story
strace -tt -T \
    -e trace=socket,bind,fcntl,recvfrom,epoll_create1,epoll_ctl,epoll_wait \
    -o "$OUT" \
    "$BIN" --mode rx --port "$PORT" &
RX_PID=$!
sleep 1

"$BIN" --mode tx --port "$PORT" --count "$COUNT" --payload 64

sleep 2          # idle window: the blocked wait shows as an unfinished call

# Signal the TRACEE, not strace: strace shields itself from SIGINT so it
# can detach cleanly, and never forwards a signal sent to its own pid.
pkill -INT -f -- "--mode rx --port $PORT" || kill -INT "$RX_PID"
for _ in $(seq 1 50); do kill -0 "$RX_PID" 2>/dev/null || break; sleep 0.1; done
kill "$RX_PID" 2>/dev/null || true   # last resort; should already be gone
wait "$RX_PID" 2>/dev/null || true

echo
echo "=== transcript: $OUT ==="
echo "--- first 15 lines ---"
head -15 "$OUT"
echo "--- last 10 lines ---"
tail -10 "$OUT"
echo
echo "--- syscall counts ---"
grep -oE '^[0-9:.]+ [a-z_0-9]+\(' "$OUT" | awk '{print $2}' | sort | uniq -c | sort -rn
