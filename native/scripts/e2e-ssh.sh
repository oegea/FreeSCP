#!/usr/bin/env bash
# End-to-end checks of a finished build against an SSH server (SFTP and SCP): connect + list,
# upload/download round trip, mkdir/rename/delete, a large file with a non-ASCII name (compared
# byte for byte), a directory tree (diff -r), failure paths, and the Qt GUI driven offscreen.
#
# Usage: native/scripts/e2e-ssh.sh <build-dir> <host> <port> <user> <password>
# The harness downloads into /tmp/dl and the GUI into /tmp/dlq (both are wiped).
set -uo pipefail

BUILD="$1"; HOST="$2"; PORT="$3"; USER_="$4"; PASS="$5"
H="$BUILD/harness/winscp-harness"
if [ -x "$BUILD/ui-qt/winscp-qt.app/Contents/MacOS/winscp-qt" ]; then APP="$BUILD/ui-qt/winscp-qt.app/Contents/MacOS/winscp-qt"
else APP="$BUILD/ui-qt/winscp-qt"; fi

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
BIG="$WORK/big ñ file 测试.bin"
head -c 20971520 /dev/urandom > "$BIG"
mkdir -p "$WORK/tree/sub dir/deeper" "$WORK/tree/empty"
echo a > "$WORK/tree/a.txt"; echo b > "$WORK/tree/sub dir/b ñ.txt"; echo c > "$WORK/tree/sub dir/deeper/c.txt"

FAILED=0
check() { if [ "$2" = ok ]; then echo "  ok    $1"; else echo "  FAIL  $1"; FAILED=1; fi; }
fresh() { rm -rf /tmp/dl; mkdir -p /tmp/dl; }

for MODE in SFTP SCP; do
  echo "== $MODE"
  EXTRA=(); [ "$MODE" = SCP ] && EXTRA=(WINSCP_SCP=1 WINSCP_UTF8=1)
  # ${EXTRA[@]+...}: an empty array is "unbound" under `set -u` in bash 3.2 (macOS)
  run() { env WINSCP_INI="$WORK/harness.ini" ${EXTRA[@]+"${EXTRA[@]}"} "$@" "$H" "$HOST" "$PORT" "$USER_" "$PASS" 2>&1; }

  fresh; OUT="$(run WINSCP_XFER=1 WINSCP_OPS=1)"; RC=$?
  { [ $RC -eq 0 ] && echo "$OUT" | grep -aq 'UPLOAD ok=1' && cmp -s /tmp/xfer-up.txt /tmp/dl/xfer-up.txt; } && r=ok || r=no
  check "$MODE connect + upload/download round trip" $r
  [ $r = ok ] || echo "$OUT" | tail -15
  { echo "$OUT" | grep -aq 'mkdir ok=1' && echo "$OUT" | grep -aq 'rename ok=1' && echo "$OUT" | grep -aq 'delete ok=1'; } && r=ok || r=no
  check "$MODE mkdir / rename / delete" $r

  fresh; run WINSCP_XFER=1 "WINSCP_XFER_FILE=$BIG" > "$WORK/out.txt"
  cmp -s "$BIG" "/tmp/dl/$(basename "$BIG")" && r=ok || { r=no; tail -8 "$WORK/out.txt"; }
  check "$MODE 20 MB file with a non-ASCII name, byte-identical" $r

  fresh; run WINSCP_XFER=1 "WINSCP_XFER_FILE=$WORK/tree" > "$WORK/out.txt"
  diff -r "$WORK/tree" /tmp/dl/tree > /dev/null 2>&1 && r=ok || { r=no; tail -8 "$WORK/out.txt"; }
  check "$MODE directory tree round trip" $r
done

echo "== failure paths"
OUT="$("$H" no-such-host.invalid 22 a b 2>&1)"; RC=$?
{ [ $RC -eq 1 ] && echo "$OUT" | grep -aq 'does not exist'; } && r=ok || r=no
check "unknown host -> readable error, exit 1" $r
[ $r = ok ] || echo "$OUT" | tail -4
OUT="$("$H" 127.0.0.1 1 a b 2>&1)"; RC=$?
{ [ $RC -eq 1 ] && echo "$OUT" | grep -aqi 'refused'; } && r=ok || r=no
check "closed port -> 'refused', exit 1" $r
[ $r = ok ] || echo "$OUT" | tail -4
OUT="$(env WINSCP_INI=/nonexistent-dir/x.ini "$H" "$HOST" "$PORT" "$USER_" "$PASS" 2>&1)"; RC=$?
{ [ $RC -eq 0 ] && echo "$OUT" | grep -aq 'CONNECTED'; } && r=ok || r=no
check "unwritable INI does not abort the connection" $r

echo "== Qt GUI (offscreen)"
rm -rf /tmp/dlq
env QT_QPA_PLATFORM=offscreen WINSCP_NO_AUTOLOGIN=1 WINSCP_SHOT="$WORK/gui.png" \
  "WINSCP_AUTOCONNECT=$HOST:$PORT:$USER_:$PASS:sftp" \
  "WINSCP_AUTOXFER=xfer-up.txt,$(basename "$BIG"),tree" "$APP" > "$WORK/gui.txt" 2>&1; RC=$?
{ [ $RC -eq 0 ] && cmp -s "$BIG" "/tmp/dlq/$(basename "$BIG")" && diff -r "$WORK/tree" /tmp/dlq/tree > /dev/null 2>&1; } && r=ok || { r=no; tail -12 "$WORK/gui.txt"; }
check "connect + 3-item queued download over parallel connections, exit 0 (got $RC)" $r

[ $FAILED -eq 0 ] && echo "ALL OK" || echo "SOME CHECKS FAILED"
exit $FAILED
