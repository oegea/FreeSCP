#!/usr/bin/env bash
# End-to-end tests of the features built on top of the engine bridge, against real servers:
#
#   conn     listing errors, key files, UTF-8 file names                    (all protocols)
#   auth     keyboard-interactive / 2FA prompts, password prompts
#   tunnel   SSH tunnel (jump host): SFTP, SCP, WebDAV, S3; parallel connections; failure paths
#   proxy    SOCKS4 / SOCKS5 / HTTP proxies, with and without authentication (all protocols)
#   ftps     FTP over TLS, explicit and implicit; active mode
#   xfer     Transfer Settings: timestamps, text mode, permissions, name case, masks, speed limit
#   resume   interrupted uploads/downloads continue from the .filepart
#   find     Find Files and directory sizes, local and remote                (all protocols)
#   sync     Synchronize: both directions, mirror with delete (time-based decisions)
#   gui      the Qt app driven offscreen: saved sites, dialogs, transfer settings, find,
#            directory sizes, Compare Directories, Compare Files
#   ops      capabilities + session information, directory delete, permissions / owner / group,
#            links, server-side duplicate and move, free space                (all protocols)
#   panels   the Qt app's panels driven offscreen: columns, status lines, hidden files, filter,
#            selection, sorting, recent directories, bookmarks, and the Properties / Add Link /
#            Duplicate / Move To / Server Information dialogs, reconnect, Keep Remote Directory
#            up to Date
#   editor   the internal editor: encodings, byte-order marks and line endings survive a save,
#            binary files are refused, find / replace / go to line / reload, remote files
#
# Usage:  native/scripts/e2e/servers.sh up <data-dir>
#         native/scripts/e2e-features.sh <build-dir> <data-dir> [section ...]     (default: all)
#         E2E_RUN="arch -x86_64" native/scripts/e2e-features.sh <universal build dir> <data-dir>   (Rosetta)
#
# Uses the bridge CLI (ui-qt/tests/bridgecli.cpp) - the same API the GUI calls - and the app's
# scripted-UI hooks (WINSCP_UITEST, see main.cpp). Everything is created under /tmp/freescp-e2e and
# in dedicated "e2e-*" directories on the servers.
set -uo pipefail

BUILD="$(cd "${1:?build dir}" && pwd)"; DATA="$(cd "${2:?data dir (servers.sh up)}" && pwd)"; shift 2
SECTIONS="${*:-conn auth tunnel proxy ftps xfer resume find sync gui ops panels editor}"
. "$DATA/env.sh"
C="$BUILD/ui-qt/freescp-bridge-cli"
if [ -x "$BUILD/ui-qt/winscp-qt.app/Contents/MacOS/winscp-qt" ]; then APP="$BUILD/ui-qt/winscp-qt.app/Contents/MacOS/winscp-qt"
else APP="$BUILD/ui-qt/winscp-qt"; fi
W="${E2E_WORK:-/tmp/freescp-e2e}"; rm -rf "$W"; mkdir -p "$W/dl" "$W/shots"     # E2E_WORK: another work directory
# E2E_RUN="arch -x86_64" runs both binaries through a launcher (the universal macOS build under
# Rosetta); E2E_CLI / E2E_APP name other binaries than the build tree's.
C="${E2E_CLI:-$C}"; APP="${E2E_APP:-$APP}"
if [ -n "${E2E_RUN:-}" ]; then
  mkdir -p "$W/bin"
  printf '#!/bin/sh\nexec %s "%s" "$@"\n' "$E2E_RUN" "$C" > "$W/bin/cli"; printf '#!/bin/sh\nexec %s "%s" "$@"\n' "$E2E_RUN" "$APP" > "$W/bin/app"
  chmod +x "$W/bin/cli" "$W/bin/app"; C="$W/bin/cli"; APP="$W/bin/app"
fi
OUT="$W/last.out"; LOG=/tmp/winscp-session.log
export WINSCP_INI="$W/engine.ini"    # private host-key / certificate cache for the bridge CLI and the app
IP="${E2E_LAN_IP:-}"; K="$DATA/keys"

FAILED=0; PASSED=0; SKIPPED=0
ok()   { echo "  ok    $1"; PASSED=$((PASSED+1)); }
bad()  { echo "  FAIL  $1"; FAILED=$((FAILED+1)); [ -f "$OUT" ] && tail -8 "$OUT" | sed 's/^/        | /'; }
skip() { echo "  skip  $1"; SKIPPED=$((SKIPPED+1)); }
chk()  { if eval "$2"; then ok "$1"; else bad "$1"; fi; }
want() { case " $SECTIONS " in *" $1 "*) return 0;; *) return 1;; esac; }
# tmo <seconds> cmd... : hard timeout (exit 142), nothing left running behind it
tmo()  { local s="$1"; shift; perl -e 'alarm shift; exec @ARGV or die "exec: $!"' "$s" "$@"; }
run()  { tmo "${TMO:-90}" "$C" "$@" > "$OUT" 2>&1; RC=$?; }
has()  { grep -aq -- "$1" "$OUT"; }
mt()   { if stat -f %m "$1" >/dev/null 2>&1; then stat -f %m "$1"; else stat -c %Y "$1"; fi; }
sz()   { if stat -f %z "$1" >/dev/null 2>&1; then stat -f %z "$1"; else stat -c %s "$1" 2>/dev/null || echo 0; fi; }
perm() { if stat -f %Lp "$1" >/dev/null 2>&1; then stat -f %Lp "$1"; else stat -c %a "$1"; fi; }
freshdl() { rm -rf "$W/dl"; mkdir -p "$W/dl"; }
proxylog() { : > "$DATA/run/$1.log"; }
inlog()  { grep -q -- "$2" "$DATA/run/$1.log"; }

SSH="port=2223 user=winscp pass=winscp123"
FTP="port=21 user=winscp pass=winscp123 proto=ftp"
DAV="port=8087 user=winscp pass=winscp123 proto=dav"
S3="port=9101 user=minioadmin pass=minioadmin123 proto=s3"
# name | options | remote base dir | the same directory on this machine
PROTOS=( "sftp|$SSH|/config|$DATA/sshhome"
         "scp|$SSH proto=scp utf8=on|/config|$DATA/sshhome"
         "ftp|$FTP|/home/winscp|$DATA/ftphome"
         "dav|$DAV||$DATA/davdata/data"
         "s3|$S3|/testbucket|$DATA/s3data/testbucket" )

#==============================================================================================
if want conn; then
  echo "== conn: listing errors are errors (never an empty directory)"
  for e in "${PROTOS[@]}"; do
    IFS='|' read -r name opts base host <<< "$e"
    run $opts -- "ls|${base}/e2e-no-such-dir" "ls|${base:-/}"
    chk "$name: missing directory is reported, the session keeps working" "[ $RC -eq 2 ] && has 'FAIL ls|' && has 'OK   ls|'"
  done
  run $SSH -- "ls|/root"; chk "sftp: unreadable directory -> 'Permission denied'" "[ $RC -eq 2 ] && has 'Permission denied'"

  echo "== conn: private keys"
  echo par > "$W/par.txt"
  run port=2223 user=winscp "key=$K/k_plain" -- ls parallel;                  chk "OpenSSH key without passphrase (+ parallel connection)" "[ $RC -eq 0 ] && has 'PARALLEL supported=1'"
  run port=2223 user=winscp "key=$K/k_enc" pass=keypass -- ls "parallel|$W/par.txt|/config"; chk "encrypted OpenSSH key + passphrase (+ upload over a parallel connection)" "[ $RC -eq 0 ]"
  run port=2223 user=winscp "key=$K/k_rsa_pem" -- ls;                          chk "RSA PEM key" "[ $RC -eq 0 ]"
  run port=2223 user=winscp "key=$K/k_enc" pass=wrong -- ls;                   chk "wrong passphrase -> clean failure" "[ $RC -eq 1 ]"
  chk "no converted key left on disk" "[ \"\$(ls /tmp/.freescp-key-* 2>/dev/null | wc -l | tr -d ' ')\" = 0 ]"

  echo "== conn: UTF-8 file names over SCP (server shell locale is not UTF-8)"
  mkdir -p "$W/utf"; echo x > "$W/utf/añ ö 测试.txt"; rm -rf "$DATA/sshhome/e2e-utf"; mkdir -p "$DATA/sshhome/e2e-utf"
  run $SSH proto=scp utf8=on -- "put|$W/utf/añ ö 测试.txt|/config/e2e-utf"
  chk "utf8=on: non-ASCII name uploaded intact" "[ $RC -eq 0 ] && [ -f \"$DATA/sshhome/e2e-utf/añ ö 测试.txt\" ]"
fi

#==============================================================================================
if want auth; then
  echo "== auth: prompts"
  run $SSH -- ls;                                               chk "stored password, no prompt" "[ $RC -eq 0 ] && ! has PROMPT"
  run port=2223 user=winscp "answers=winscp123" -- ls parallel; chk "no stored password: asked once, then reused for parallel connections" "[ $RC -eq 0 ] && has 'PROMPT kind=7' && has 'PARALLEL supported=1'"
  run port=2223 user=winscp pass=nope "answers=winscp123" -- ls; chk "wrong stored password: asked, right one accepted" "[ $RC -eq 0 ] && has PROMPT"
  run port=2223 user=winscp pass=nope -- ls;                    chk "wrong password, nobody to ask: fails, does not loop" "[ $RC -eq 1 ] && has 'Authentication failed'"
  if [ "${E2E_HAVE_KBDINT:-0}" = 1 ]; then
    run port=2231 user=twofa pass=pw-secret "answers=pw-secret|246810" -- ls parallel
    chk "2FA, password + code in one challenge (two fields; parallel connections disabled)" "[ $RC -eq 0 ] && has 'fields=2' && has 'PARALLEL supported=0'"
    run port=2231 user=twofa pass=pw-secret "answers=pw-secret|000000|pw-secret|246810" -- ls
    chk "2FA, wrong code: asked again, then accepted" "[ $RC -eq 0 ] && [ \"\$(grep -ac '^PROMPT' $OUT)\" = 2 ]"
    run port=2231 user=twofa pass=pw-secret "answers=" -- ls
    chk "2FA cancelled by the user -> clean failure" "[ $RC -eq 1 ]"
    run port=2232 user=twofa pass=pw-secret "answers=246810" -- ls
    chk "2FA in two rounds: stored password answers the first, the code is asked" "[ $RC -eq 0 ] && has 'Verification code' && [ \"\$(grep -ac '^PROMPT' $OUT)\" = 1 ]"
    run port=2232 user=twofa "answers=pw-secret|246810" -- ls
    chk "2FA in two rounds without a stored password: both asked" "[ $RC -eq 0 ] && [ \"\$(grep -ac '^PROMPT' $OUT)\" = 2 ]"
    run port=2233 user=twofa pass=whatever "answers=246810" -- ls
    chk "one-time code only (hidden field): the stored password is tried, then the user is asked" "[ $RC -eq 0 ] && has 'One-time code'"
    run port=2231 user=twofa pass=pw-secret -- ls
    chk "2FA with nobody to ask: fails, does not loop" "[ $RC -eq 1 ]"
  else skip "2FA servers not available (asyncssh)"; fi
fi

#==============================================================================================
if want tunnel; then
  echo "== tunnel: SSH jump host (targets are addressed by a name only the jump host can resolve)"
  J="tunnel=127.0.0.1:2224:jumper:jump456"; T="host=freescp-e2e-sshd port=2222 user=winscp"
  head -c 8000000 /dev/urandom > "$W/tun.bin"; echo par > "$W/par.txt"
  freshdl; run $T pass=winscp123 $J -- "put|$W/tun.bin|/config" "get|/config/tun.bin|$W/dl" "rm|tun.bin"
  chk "SFTP through the tunnel: 8 MB round trip, byte-identical" "[ $RC -eq 0 ] && cmp -s $W/tun.bin $W/dl/tun.bin"
  run $T pass=winscp123 proto=scp $J -- ls;                            chk "SCP through the tunnel" "[ $RC -eq 0 ]"
  run $T pass=winscp123 $J -- ls "parallel|$W/par.txt|/config";        chk "parallel connection opens its own tunnel and uploads" "[ $RC -eq 0 ] && has 'PARALLEL supported=1'"
  run $T pass=winscp123 "tunnel=127.0.0.1:2224:jumper:keypass" "tunnelkey=$K/k_enc" -- ls; chk "jump host by (encrypted) key, target by password" "[ $RC -eq 0 ]"
  run $T "key=$K/k_plain" $J -- ls;                                    chk "jump host by password, target by key" "[ $RC -eq 0 ]"
  freshdl; run host=freescp-e2e-dav port=80 user=winscp pass=winscp123 proto=dav $J -- "put|$W/tun.bin|/" "get|/tun.bin|$W/dl" "rm|tun.bin"
  chk "WebDAV through the tunnel: 8 MB round trip" "[ $RC -eq 0 ] && cmp -s $W/tun.bin $W/dl/tun.bin"
  freshdl; run host=freescp-e2e-s3 port=9000 user=minioadmin pass=minioadmin123 proto=s3 $J -- "ls|/testbucket" "put|$W/tun.bin|/testbucket" "get|/testbucket/tun.bin|$W/dl"
  chk "S3 through the tunnel: 8 MB round trip (several forwarded connections)" "[ $RC -eq 0 ] && cmp -s $W/tun.bin $W/dl/tun.bin"
  run $T pass=winscp123 "tunnel=127.0.0.1:2224:jumper:wrong" -- ls;    chk "wrong jump password, nobody to ask -> authentication failure" "[ $RC -eq 1 ] && has 'Authentication failed'"
  run $T pass=winscp123 "tunnel=127.0.0.1:2224:jumper:wrong" "answers=jump456" -- ls
  chk "wrong jump password: the prompt says it is for the tunnel, right one accepted" "[ $RC -eq 0 ] && has 'tunnel=1' && has 'Authenticating tunnel through'"
  run $T pass=winscp123 "tunnel=127.0.0.1:2299:jumper:jump456" -- ls;  chk "jump host down -> refused" "[ $RC -eq 1 ] && grep -aqi refused $OUT"
  run host=e2e-no-such-box port=2222 user=winscp pass=winscp123 $J -- ls; chk "target unknown to the jump host -> tunnel error" "[ $RC -eq 1 ] && has 'Error while tunneling'"
  run host=freescp-e2e-jump port=2222 user=jumper pass=jump456 "tunnel=127.0.0.1:2223:winscp:winscp123" -- ls
  chk "jump host forbids forwarding -> 'Administratively prohibited'" "[ $RC -eq 1 ] && has 'Administratively prohibited'"
  run host=freescp-e2e-ftp port=21 user=winscp pass=winscp123 proto=ftp $J -- ls
  chk "FTP through the tunnel: the data connection cannot be tunnelled -> an error, not an empty listing" "[ $RC -eq 2 ] && has 'Error listing directory'"
  T0=$(date +%s); /usr/bin/time -p "$C" $T pass=winscp123 $J -- "sleep|8" > "$OUT" 2>&1
  CPU=$(awk '/^user/{u=$2} /^sys/{s=$2} END{printf "%d", (u+s)*100}' "$OUT")
  chk "an idle tunnelled session does not spin the CPU (${CPU} cs of CPU in 8 s)" "[ ${CPU:-999} -lt 150 ]"
fi

#==============================================================================================
if want proxy; then
  echo "== proxy"
  if [ -z "$IP" ]; then skip "no non-localhost address of this machine (SFTP/SCP never proxy localhost)"; else
    B="host=$IP port=2223 user=winscp pass=winscp123"
    proxylog socks;     run $B proxy=socks5:127.0.0.1:11080 -- ls;             chk "SFTP via SOCKS5" "[ $RC -eq 0 ] && inlog socks 'socks5 $IP:2223'"
    proxylog socks;     run $B proxy=socks4:127.0.0.1:11080 -- ls;             chk "SFTP via SOCKS4" "[ $RC -eq 0 ] && inlog socks 'socks4 $IP:2223'"
    proxylog socks;     run $B proto=scp proxy=socks5:127.0.0.1:11080 -- ls;   chk "SCP via SOCKS5" "[ $RC -eq 0 ] && inlog socks 'socks5 $IP:2223'"
    proxylog socksauth; run $B proxy=socks5:127.0.0.1:11081:puser:ppass -- ls; chk "SOCKS5 with user/password" "[ $RC -eq 0 ] && inlog socksauth 'auth=puser'"
    run $B proxy=socks5:127.0.0.1:11081:puser:wrong -- ls;                     chk "SOCKS5 wrong password -> 'rejected our password'" "[ $RC -eq 1 ] && has 'rejected our password'"
    proxylog http;      run $B proxy=http:127.0.0.1:13128 -- ls;               chk "SFTP via HTTP CONNECT" "[ $RC -eq 0 ] && inlog http 'http $IP:2223'"
    proxylog httpauth;  run $B proxy=http:127.0.0.1:13129:puser:ppass -- ls;   chk "HTTP proxy with Basic auth (407, retry on the same connection)" "[ $RC -eq 0 ] && inlog httpauth 'auth=puser'"
    proxylog httpclose; run $B proxy=http:127.0.0.1:13130:puser:ppass -- ls;   chk "HTTP proxy that closes after 407 (reconnect)" "[ $RC -eq 0 ] && inlog httpclose 'auth=puser'"
    run $B proxy=http:127.0.0.1:13129:puser:wrong -- ls;                       chk "HTTP proxy wrong password, nobody to ask: fails, does not loop" "[ $RC -eq 1 ]"
    run $B proxy=http:127.0.0.1:13129:puser:wrong "answers=ppass" -- ls;       chk "HTTP proxy wrong password: asked ('Proxy password'), accepted" "[ $RC -eq 0 ] && has 'Proxy'"
    run $B proxy=socks5:127.0.0.1:11099 -- ls;                                 chk "proxy down -> refused" "[ $RC -eq 1 ] && grep -aqi refused $OUT"
    proxylog socks; run host=freescp-e2e-sshd port=2222 user=winscp pass=winscp123 "tunnel=$IP:2224:jumper:jump456" proxy=socks5:127.0.0.1:11080 -- ls
    chk "tunnel + proxy: the jump host is reached through the proxy" "[ $RC -eq 0 ] && inlog socks 'socks5 $IP:2224'"
  fi
  head -c 3000000 /dev/urandom > "$W/px.bin"
  for kind in socks5:127.0.0.1:11080 socks4:127.0.0.1:11080 http:127.0.0.1:13128; do
    lg=socks; [ "${kind%%:*}" = http ] && lg=http
    proxylog $lg; freshdl; run $FTP proxy=$kind -- "put|$W/px.bin|/home/winscp" "get|/home/winscp/px.bin|$W/dl"
    chk "FTP via ${kind%%:*}: control + data connections through the proxy, 3 MB round trip" "[ $RC -eq 0 ] && cmp -s $W/px.bin $W/dl/px.bin && [ \$(wc -l < $DATA/run/$lg.log) -ge 3 ]"
  done
  proxylog socks; run $DAV proxy=socks5:127.0.0.1:11080 -- ls;       chk "WebDAV via SOCKS5" "[ $RC -eq 0 ] && inlog socks ':8087'"
  proxylog http;  run $DAV proxy=http:127.0.0.1:13128 -- ls;         chk "WebDAV via HTTP proxy" "[ $RC -eq 0 ] && inlog http ':8087'"
  proxylog httpauth; run $DAV proxy=http:127.0.0.1:13129:puser:ppass -- ls; chk "WebDAV via HTTP proxy with auth" "[ $RC -eq 0 ] && inlog httpauth 'auth=puser'"
  proxylog socks; run $S3 proxy=socks5:127.0.0.1:11080 -- "ls|/testbucket"; chk "S3 via SOCKS5" "[ $RC -eq 0 ] && inlog socks ':9101'"
  proxylog http;  run $S3 proxy=http:127.0.0.1:13128 -- "ls|/testbucket";   chk "S3 via HTTP proxy" "[ $RC -eq 0 ] && inlog http ':9101'"
fi

#==============================================================================================
if want ftps; then
  echo "== ftps"
  head -c 2000000 /dev/urandom > "$W/ftps.bin"
  freshdl; run port=2121 user=winscp pass=winscp123 proto=ftp tls=1 -- "put|$W/ftps.bin|/home/winscp" ls "get|/home/winscp/ftps.bin|$W/dl"
  chk "explicit TLS: certificate shown for confirmation, 2 MB round trip over an encrypted data channel" "[ $RC -eq 0 ] && has CONFIRM && cmp -s $W/ftps.bin $W/dl/ftps.bin"
  freshdl; run port=2990 user=winscp pass=winscp123 proto=ftp tls=implicit -- "put|$W/ftps.bin|/home/winscp" "get|/home/winscp/ftps.bin|$W/dl"
  chk "implicit TLS: 2 MB round trip" "[ $RC -eq 0 ] && cmp -s $W/ftps.bin $W/dl/ftps.bin"
  run port=2121 user=winscp pass=winscp123 proto=ftp -- ls
  chk "plain FTP to a TLS-only server -> the server's reason is shown" "[ $RC -eq 1 ] && has 'must use encryption'"
  run $FTP passive=0 -- ls; chk "active mode listing" "[ $RC -eq 0 ]"
fi

#==============================================================================================
if want xfer; then
  X="$W/x"; mkdir -p "$X"
  head -c 300000 /dev/urandom > "$X/data.bin"; touch -t 202508091011.12 "$X/data.bin"
  printf 'line1\nline2\nline3\n' > "$X/lf.txt"; touch -t 202602030405.06 "$X/lf.txt"
  printf 'line1\r\nline2\r\nline3\r\n' > "$X/crlf.txt"
  printf 'a\nb\n' > "$X/page.html"; head -c 4096 /dev/urandom > "$X/blob.bin"; printf '\n\n\n' >> "$X/blob.bin"
  for e in "${PROTOS[@]}"; do
    IFS='|' read -r name opts base host <<< "$e"
    R="$base/e2e-x"; H="$host/e2e-x"
    echo "== xfer: timestamps ($name)"
    rm -rf "$H"; mkdir -p "$H"; chmod 777 "$H" 2>/dev/null; freshdl
    run $opts -- "put|$X/data.bin|$R" "get|$R/data.bin|$W/dl"
    chk "$name: round trip" "[ $RC -eq 0 ] && cmp -s $X/data.bin $W/dl/data.bin"
    # Apache mod_dav and S3 stamp the object with the upload time: "preserve" then means the
    # download carries the SERVER's time.
    REF="$X/data.bin"; [ "$name" = dav ] && REF="$H/data.bin"; [ "$name" = s3 ] && REF="$H/data.bin/xl.meta"
    if [ "$name" != dav ] && [ "$name" != s3 ]; then chk "$name: upload keeps the modification time on the server" "[ \"\$(mt $H/data.bin)\" = \"\$(mt $X/data.bin)\" ]"; fi
    chk "$name: download keeps the modification time" "[ \"\$(mt $W/dl/data.bin)\" = \"\$(mt $REF)\" ]"
    freshdl; run $opts -- "set|time=0" "put|$X/lf.txt|$R" "get|$R/lf.txt|$W/dl"
    chk "$name: with 'preserve timestamp' off the download is stamped now" "[ $RC -eq 0 ] && [ \$(( \$(date +%s) - \$(mt $W/dl/lf.txt) )) -lt 120 ]"
    head -c 1500000 /dev/urandom > "$X/speed.bin"; freshdl
    run $opts -- "put|$X/speed.bin|$R"
    T1=$(date +%s); run $opts -- "set|speed=300" "get|$R/speed.bin|$W/dl"; T2=$(date +%s)
    chk "$name: download limited to 300 KB/s takes $((T2-T1)) s for 1.5 MB (about 5)" "[ $RC -eq 0 ] && [ $((T2-T1)) -ge 3 ] && [ $((T2-T1)) -le 10 ] && cmp -s $X/speed.bin $W/dl/speed.bin"
    T1=$(date +%s); run $opts -- "set|speed=300" "put|$X/speed.bin|$R"; T2=$(date +%s)
    chk "$name: upload limited to 300 KB/s takes $((T2-T1)) s" "[ $RC -eq 0 ] && [ $((T2-T1)) -ge 3 ] && [ $((T2-T1)) -le 10 ]"
  done
  H="$DATA/sshhome/e2e-x"; R=/config/e2e-x
  for proto in sftp scp; do
    echo "== xfer: text mode ($proto)"
    rm -rf "$H"; mkdir -p "$H"; freshdl
    run $SSH proto=$proto -- "set|mode=text" "put|$X/lf.txt|$R" "get|$R/lf.txt|$W/dl"
    chk "$proto: text mode, LF server: nothing to convert" "[ $RC -eq 0 ] && cmp -s $X/lf.txt $H/lf.txt && cmp -s $X/lf.txt $W/dl/lf.txt"
    rm -rf "$H"; mkdir -p "$H"; freshdl
    run $SSH proto=$proto crlf=1 -- "set|mode=text" "put|$X/lf.txt|$R" "get|$R/lf.txt|$W/dl"
    chk "$proto: text mode, CR/LF server: stored as CR/LF, downloaded back as LF" "[ $RC -eq 0 ] && cmp -s $X/crlf.txt $H/lf.txt && cmp -s $X/lf.txt $W/dl/lf.txt"
    rm -rf "$H"; mkdir -p "$H"
    run $SSH proto=$proto crlf=1 -- "set|mode=binary" "put|$X/lf.txt|$R"
    chk "$proto: binary mode leaves the bytes alone" "[ $RC -eq 0 ] && cmp -s $X/lf.txt $H/lf.txt"
    rm -rf "$H"; mkdir -p "$H"; freshdl
    run $SSH proto=$proto crlf=1 -- "set|mode=auto" "put|$X/page.html|$R" "put|$X/blob.bin|$R" "get|$R/blob.bin|$W/dl"
    chk "$proto: automatic mode: .html as text, .bin untouched both ways" "[ $RC -eq 0 ] && [ \"\$(xxd -p $H/page.html)\" = 610d0a620d0a ] && cmp -s $X/blob.bin $H/blob.bin && cmp -s $X/blob.bin $W/dl/blob.bin"
    rm -rf "$H"; mkdir -p "$H"
    run $SSH proto=$proto crlf=1 -- "set|mode=auto|textmask=*.bin" "put|$X/page.html|$R"
    chk "$proto: automatic mode with a custom text mask" "[ $RC -eq 0 ] && cmp -s $X/page.html $H/page.html"
    rm -rf "$H"; mkdir -p "$H" "$X/pdir/in"; echo x > "$X/pdir/in/f.txt"
    run $SSH proto=$proto -- "set|rights=600" "put|$X/lf.txt|$R" "put|$X/pdir|$R"
    chk "$proto: 'set permissions 600': files 600, directories 700" "[ $RC -eq 0 ] && [ \"\$(perm $H/lf.txt)\" = 600 ] && [ \"\$(perm $H/pdir)\" = 700 ] && [ \"\$(perm $H/pdir/in/f.txt)\" = 600 ]"
  done
  echo "== xfer: names, masks, newer-only"
  rm -rf "$H"; mkdir -p "$H"; freshdl; cp "$X/lf.txt" "$X/MixedCase.TXT"
  run $SSH -- "set|case=lower" "put|$X/MixedCase.TXT|$R" "set|case=upper" "get|$R/mixedcase.txt|$W/dl"
  chk "file name case: lower on upload, UPPER on download" "[ $RC -eq 0 ] && [ -f $H/mixedcase.txt ] && ls $W/dl | grep -qx MIXEDCASE.TXT"
  rm -rf "$X/tree"; mkdir -p "$X/tree/sub"; echo 1 > "$X/tree/a.txt"; echo 2 > "$X/tree/b.tmp"; echo 3 > "$X/tree/sub/c.txt"; echo 4 > "$X/tree/sub/d.log"; echo 5 > "$X/tree/.hidden"
  files() { (cd "$H/tree" 2>/dev/null && find . -type f | sort | tr '\n' ' '); }
  rm -rf "$H"; mkdir -p "$H"; run $SSH -- "set|mask=*.txt" "put|$X/tree|$R"
  chk "mask '*.txt'" "[ $RC -eq 0 ] && [ \"\$(files)\" = './a.txt ./sub/c.txt ' ]"
  rm -rf "$H"; mkdir -p "$H"; run $SSH -- "set|mask=^ *.tmp; *.log" "put|$X/tree|$R"
  chk "mask '| *.tmp; *.log' (exclusion)" "[ $RC -eq 0 ] && [ \"\$(files)\" = './.hidden ./a.txt ./sub/c.txt ' ]"
  rm -rf "$H"; mkdir -p "$H"; run $SSH -- "set|hidden=exclude" "put|$X/tree|$R"
  chk "exclude hidden files" "[ $RC -eq 0 ] && [ ! -e $H/tree/.hidden ] && [ -f $H/tree/a.txt ]"
  run $SSH -- "checkmask|*.txt; *.htm ^ *.bak"; chk "a valid mask is accepted" "[ $RC -eq 0 ]"
  run $SSH -- "checkmask|*>12zz";               chk "a malformed mask is rejected" "[ $RC -eq 2 ]"
  rm -rf "$H"; mkdir -p "$H"; echo OLD > "$X/n.txt"; touch -t 202001010000 "$X/n.txt"
  docker exec freescp-e2e-sshd sh -c 'echo NEWER-ON-SERVER > /config/e2e-x/n.txt' 2>/dev/null || echo NEWER-ON-SERVER > "$H/n.txt"
  sleep 2; run $SSH -- "set|newer=1" "put|$X/n.txt|$R"
  chk "newer only: an older local file does not replace a newer remote one" "grep -q NEWER-ON-SERVER $H/n.txt"
  sleep 2; touch "$X/n.txt"; run $SSH -- "set|newer=1" "put|$X/n.txt|$R"
  chk "newer only: a newer local file does" "grep -q OLD $H/n.txt"
fi

#==============================================================================================
if want resume; then
  echo "== resume (SFTP)"
  H="$DATA/sshhome/e2e-r"; R=/config/e2e-r; rm -rf "$H"; mkdir -p "$H"
  head -c 3000000 /dev/urandom > "$W/res.bin"
  ( "$C" $SSH -- "set|speed=300" "put|$W/res.bin|$R" > /dev/null 2>&1 & P=$!; sleep 5; kill -9 $P ) 2>/dev/null; sleep 1
  PART=$(sz "$H/res.bin.filepart")
  chk "upload killed half-way: $PART of 3000000 bytes in a .filepart, no final file" "[ $PART -gt 100000 ] && [ $PART -lt 3000000 ] && [ ! -e $H/res.bin ]"
  rm -f "$LOG"; WINSCP_SESSIONLOG=1 tmo 60 "$C" $SSH -- "put|$W/res.bin|$R" > "$OUT" 2>&1; RC=$?
  chk "retry resumes instead of starting over" "[ $RC -eq 0 ] && grep -aq 'Resuming file transfer' $LOG"
  chk "resumed upload is byte-identical, the .filepart is gone" "cmp -s $W/res.bin $H/res.bin && [ ! -e $H/res.bin.filepart ]"
  freshdl
  ( "$C" $SSH -- "set|speed=300" "get|$R/res.bin|$W/dl" > /dev/null 2>&1 & P=$!; sleep 5; kill -9 $P ) 2>/dev/null
  PART=$(sz "$W/dl/res.bin.filepart")
  chk "download killed half-way: local .filepart of $PART bytes" "[ $PART -gt 100000 ] && [ $PART -lt 3000000 ] && [ ! -e $W/dl/res.bin ]"
  rm -f "$LOG"; WINSCP_SESSIONLOG=1 tmo 60 "$C" $SSH -- "get|$R/res.bin|$W/dl" > "$OUT" 2>&1; RC=$?
  chk "retry resumes" "[ $RC -eq 0 ] && grep -aq 'Partially transferred file exists' $LOG && grep -aq 'Resuming file transfer' $LOG"
  chk "resumed download is byte-identical, the .filepart is gone" "cmp -s $W/res.bin $W/dl/res.bin && [ ! -e $W/dl/res.bin.filepart ]"
  rm -rf "$H"; mkdir -p "$H"
  ( "$C" $SSH -- "set|speed=300|resume=off" "put|$W/res.bin|$R" > /dev/null 2>&1 & P=$!; sleep 4; kill -9 $P ) 2>/dev/null; sleep 1
  chk "resume off: written straight to the final name" "[ ! -e $H/res.bin.filepart ] && [ -e $H/res.bin ]"
  rm -rf "$H"; mkdir -p "$H"
  ( "$C" $SSH -- "set|speed=300|threshold=10000" "put|$W/res.bin|$R" > /dev/null 2>&1 & P=$!; sleep 4; kill -9 $P ) 2>/dev/null; sleep 1
  chk "smart resume with a 10 MB threshold: no .filepart for a 3 MB file" "[ ! -e $H/res.bin.filepart ] && [ -e $H/res.bin ]"
  # (created inside the container: a file written through the bind mount can be stat'ed stale for a moment)
  if docker exec freescp-e2e-sshd sh -c 'rm -rf /config/e2e-r; mkdir -p /config/e2e-r; head -c 4000000 /dev/urandom > /config/e2e-r/res.bin.filepart; chmod -R 777 /config/e2e-r' 2>/dev/null; then
    run $SSH -- "put|$W/res.bin|$R"; sleep 1
    chk "a .filepart larger than the source is discarded and the upload starts over" "[ $RC -eq 0 ] && cmp -s $W/res.bin $H/res.bin && [ ! -e $H/res.bin.filepart ]"
  else skip "docker exec not available for the oversized-.filepart case"; fi
fi

#==============================================================================================
mktree() {   # 1072 bytes in 6 files, 5 directories
  T="$W/e2e-tree"; rm -rf "$T"; mkdir -p "$T/sub/deep" "$T/empty" "$T/sp ace"
  head -c 10 /dev/zero > "$T/a.txt"; head -c 20 /dev/zero > "$T/b.log"; head -c 30 /dev/zero > "$T/sub/c.txt"
  head -c 1000 /dev/zero > "$T/sub/d.bin"; head -c 5 /dev/zero > "$T/sub/deep/e.txt"; head -c 7 /dev/zero > "$T/sp ace/ñ.txt"
}
found() { grep -a "FOUND" "$OUT" | sed -E 's/.*FOUND . +[0-9]+ ([^ ].*)   \(dir=.*/\1/' | sed "s|^$1/||" | sort | tr '\n' ','; }
if want find; then
  mktree
  echo "== find / sizes: local"
  run $SSH -- "lfind|$T|*.txt";              chk "find *.txt" "[ \"\$(found $T)\" = 'a.txt,sp ace/ñ.txt,sub/c.txt,sub/deep/e.txt,' ]"
  run $SSH -- "lfind|$T|*.txt; *.log ^ c.*"; chk "find with include | exclude" "[ \"\$(found $T)\" = 'a.txt,b.log,sp ace/ñ.txt,sub/deep/e.txt,' ]"
  run $SSH -- "lfind|$T|*>500";              chk "find by size (*>500)" "[ \"\$(found $T)\" = 'sub/d.bin,' ]"
  run $SSH -- "lfind|$T|deep/";              chk "find a directory (deep/)" "[ \"\$(found $T)\" = 'sub/deep,' ]"
  run $SSH -- "lfind|$T|*|2";                chk "the search stops when asked" "has 'matches=2 '"
  run $SSH -- "lfind|$W/e2e-nope|*";         chk "missing directory -> error" "[ $RC -eq 2 ] && has 'does not exist'"
  run $SSH -- "lfind|$T|*>12zz";             chk "malformed mask -> error" "[ $RC -eq 2 ]"
  run $SSH -- "ldu|$T";                      chk "size of the tree" "has 'DU bytes=1072 files=6 dirs=5'"
  for e in "${PROTOS[@]}"; do
    IFS='|' read -r name opts base host <<< "$e"
    R="$base/e2e-tree"
    echo "== find / sizes: $name"
    run $opts -- "ls|${base:-/}" "rm|e2e-tree"; run $opts -- "put|$T|${base:-/}"
    chk "$name: tree uploaded" "[ $RC -eq 0 ]"
    run $opts -- "find|$R|*.txt";              chk "$name: find *.txt" "[ \"\$(found $R)\" = 'a.txt,sp ace/ñ.txt,sub/c.txt,sub/deep/e.txt,' ]"
    run $opts -- "find|$R|*.txt; *.log ^ c.*"; chk "$name: find with include | exclude" "[ \"\$(found $R)\" = 'a.txt,b.log,sp ace/ñ.txt,sub/deep/e.txt,' ]"
    run $opts -- "find|$R|*>500";              chk "$name: find by size" "[ \"\$(found $R)\" = 'sub/d.bin,' ]"
    run $opts -- "find|$R|*|2";                chk "$name: the search stops when asked" "has 'matches=2 '"
    run $opts -- "find|$R/e2e-nope|*";         chk "$name: missing directory: no results, no crash" "has 'matches=0'"
    run $opts -- "find|$R|*>12zz";             chk "$name: malformed mask -> error" "[ $RC -eq 2 ]"
    run $opts -- "ls|${base:-/}" "du|e2e-tree";    chk "$name: size of the tree" "has 'DU bytes=1072 '"
  done
fi

#==============================================================================================
if want sync; then
  changes() { grep -aE "^  (Upload|Download|Delete)" "$OUT" | sed 's/^  //' | sort | tr '\n' ';'; }
  for e in "sftp|$SSH|/config/e2e-sy|$DATA/sshhome/e2e-sy" "ftp|$FTP|/home/winscp/e2e-sy|$DATA/ftphome/e2e-sy"; do
    IFS='|' read -r name opts R H <<< "$e"
    echo "== sync ($name)"
    L="$W/sy-$name"; rm -rf "$L" "$H"; mkdir -p "$L/sub" "$H/sub"; chmod -R 777 "$H"
    echo same > "$L/same.txt"; echo localnew > "$L/local-only.txt"; echo subfile > "$L/sub/in-sub.txt"
    echo v2-local > "$L/newer-local.txt"; echo v1 > "$L/older-local.txt"
    run $opts -- "sync|$L|$R|up";          chk "$name: first upload sync" "[ $RC -eq 0 ] && [ -f $H/same.txt ] && [ -f $H/sub/in-sub.txt ]"
    run $opts -- "sync|$L|$R|up|dry";      chk "$name: in sync afterwards (upload direction)" "has 'SYNC 0 change'"
    run $opts -- "sync|$L|$R|down|dry";    chk "$name: in sync afterwards (download direction)" "has 'SYNC 0 change'"
    sleep 61   # FTP listings have minute precision: make "newer" unambiguous
    echo v3-local-edited > "$L/newer-local.txt"
    rm -f "$H/older-local.txt"   # (replaced, not rewritten: on a Linux host it belongs to the server's user)
    echo remote-edited-v2 > "$H/older-local.txt"; echo remote-only > "$H/remote-only.txt"; chmod 666 "$H"/*.txt 2>/dev/null
    sleep 2
    run $opts -- "sync|$L|$R|up|dry";      chk "$name: upload sync = the locally newer file only" "[ \"\$(changes)\" = 'Upload newer-local.txt;' ]"
    run $opts -- "sync|$L|$R|down|dry";    chk "$name: download sync = remotely newer + remote-only" "[ \"\$(changes)\" = 'Download older-local.txt;Download remote-only.txt;' ]"
    run $opts -- "sync|$L|$R|both|dry";    chk "$name: both directions" "[ \"\$(changes)\" = 'Download older-local.txt;Download remote-only.txt;Upload newer-local.txt;' ]"
    run $opts -- "sync|$L|$R|up|delete|dry"; chk "$name: mirror upload with delete" "[ \"\$(changes)\" = 'Delete remote remote-only.txt;Upload newer-local.txt;' ]"
    run $opts -- "sync|$L|$R|both";        chk "$name: applying 'both' converges" "[ $RC -eq 0 ] && grep -q v3-local-edited $H/newer-local.txt && grep -q remote-edited-v2 $L/older-local.txt && [ -f $L/remote-only.txt ]"
    run $opts -- "sync|$L|$R|both|dry";    chk "$name: and is in sync again" "has 'SYNC 0 change'"
    rm -f "$H/same.txt"; mkdir -p "$L/gone-dir"; echo x > "$L/gone-dir/f.txt"; mkdir -p "$L/gone-empty"; echo y > "$L/extra-local.txt"; sleep 2
    run $opts -- "sync|$L|$R|down|delete"
    chk "$name: mirror download with delete removes local extras (file, directory with content, empty directory)" "[ $RC -eq 0 ] && [ ! -e $L/same.txt ] && [ ! -e $L/gone-dir ] && [ ! -e $L/gone-empty ] && [ ! -e $L/extra-local.txt ] && [ -f $L/newer-local.txt ] && [ -f $L/sub/in-sub.txt ]"
    echo n > "$L/sub/new-in-sub.txt"; mkdir -p "$L/sub/deeper"; echo d > "$L/sub/deeper/d.txt"
    run $opts -- "sync|$L|$R|up|dry"
    chk "$name: a change below the top directory is listed with its path" "[ \"\$(changes)\" = 'Upload sub/deeper/;Upload sub/new-in-sub.txt;' ]"
    rm -rf "$L/sub/new-in-sub.txt" "$L/sub/deeper"
    # A remote directory that cannot be read must never be taken for an empty one: mirroring it
    # "with delete" would remove every local file.
    N0="$(find "$L" | wc -l | tr -d ' ')"
    run $opts -- "sync|$L|$R-e2e-nope|down|delete" "sync|$L|$R-e2e-nope|up|delete" "sync|$L|$R|up|dry"
    chk "$name: a missing remote directory is an error in both directions; nothing is deleted or uploaded" \
        "[ $RC -eq 2 ] && [ \$(grep -c '^FAIL sync' $OUT) = 2 ] && [ \"\$(find $L | wc -l | tr -d ' ')\" = \"$N0\" ] && [ ! -e $H-e2e-nope ] && has 'SYNC 0 change'"
    run $opts -- "sync|$W/e2e-no-such-local|$R|up|delete" "sync|$W/e2e-no-such-local|$R|down" "sync|$L|$R|up|dry"
    chk "$name: a missing local directory is an error; the remote directory is left alone" \
        "[ $RC -eq 2 ] && [ \$(grep -c 'does not exist' $OUT) = 2 ] && [ ! -e $W/e2e-no-such-local ] && [ -f $H/newer-local.txt ] && has 'SYNC 0 change'"
  done
  run $SSH -- "sync|$W/sy-sftp|/root|down|delete" "ls|/config"
  chk "sftp: an unreadable remote directory is an error, the local files stay, the session keeps working" "[ $RC -eq 2 ] && has '^FAIL sync' && [ -f $W/sy-sftp/newer-local.txt ] && has '^OK   ls|'"
fi

#==============================================================================================
if want gui; then
  echo "== gui (offscreen)"
  SH="$W/shots"; GO="$W/gui.out"
  export QT_QPA_PLATFORM=offscreen WINSCP_NO_AUTOLOGIN=1
  AC="WINSCP_AUTOCONNECT=127.0.0.1:2223:winscp:winscp123:sftp"
  gui() { tmo 120 env "$@" "$APP" > "$GO" 2>&1; RC=$?; cp "$GO" "$OUT"; }
  ghas() { grep -aq -- "$1" "$GO"; }
  marked() { awk -v side="$1" '$0 ~ "panel " side {p=1; next} /^\[panel/{p=0} p && /^  \*/{print $2}' "$GO" | tr '\n' ','; }
  # scripted runs use their own settings store; start from a clean one
  defaults delete com.winscp-native-port-test.WinSCP >/dev/null 2>&1; rm -rf "$HOME/.config/WinSCP-native-port-test" 2>/dev/null

  gui WINSCP_DIALOG_SHOTS=$SH WINSCP_UITEST="login";                       chk "Login dialog renders" "[ $RC -eq 0 ] && [ -s $SH/login.png ]"
  gui WINSCP_DIALOG_SHOTS=$SH WINSCP_LOGIN_OPEN_ADVANCED=1 WINSCP_ADVANCED_TAB=2 WINSCP_UITEST="login"; chk "Advanced Site Settings render" "[ -s $SH/advanced.png ]"
  gui WINSCP_DIALOG_SHOTS=$SH WINSCP_UITEST="xfersettings";                chk "Transfer Settings dialog renders" "[ -s $SH/transfersettings.png ]"
  gui WINSCP_DIALOG_SHOTS=$SH WINSCP_UITEST="prefs";                       chk "Preferences render" "[ -s $SH/preferences.png ]"

  # "a newer version is available": the answer comes from a local file, never from the network
  UPD="$W/upd"; mkdir -p "$UPD"; rm -f "$SH/update.png"
  echo '{"tag_name":"v99.0.0"}' > "$UPD/new.json"; echo '{"tag_name":"v0.0.1"}' > "$UPD/old.json"; echo '<html>not json</html>' > "$UPD/bad.json"
  gui WINSCP_UPDATE_URL="file://$UPD/new.json" WINSCP_DIALOG_SHOTS=$SH WINSCP_DIALOG_ACCEPT=update WINSCP_UITEST="wait|3000"
  chk "update: a newer version shows the notice; Update opens the releases page" "[ $RC -eq 0 ] && [ -s $SH/update.png ] && ghas 'newer=1' && ghas 'open https://freescp.com/releases.html'"
  rm -f "$SH/update.png"; gui WINSCP_UPDATE_URL="file://$UPD/new.json" WINSCP_DIALOG_SHOTS=$SH WINSCP_UITEST="wait|3000"
  chk "update: Close does nothing else" "[ $RC -eq 0 ] && [ -s $SH/update.png ] && ! ghas 'open https'"
  rm -f "$SH/update.png"; gui WINSCP_UPDATE_URL="file://$UPD/old.json" WINSCP_DIALOG_SHOTS=$SH WINSCP_UITEST="wait|3000"
  chk "update: no notice when up to date" "[ $RC -eq 0 ] && ghas 'newer=0' && [ ! -e $SH/update.png ]"
  gui WINSCP_UPDATE_URL="file://$UPD/bad.json" WINSCP_DIALOG_SHOTS=$SH WINSCP_UITEST="wait|3000"
  chk "update: an unusable answer is ignored" "[ $RC -eq 0 ] && ghas 'check failed' && [ ! -e $SH/update.png ]"
  gui WINSCP_UPDATE_URL="file://$UPD/missing.json" WINSCP_DIALOG_SHOTS=$SH WINSCP_UITEST="wait|3000"
  chk "update: a failed request is ignored" "[ $RC -eq 0 ] && ghas 'check failed' && [ ! -e $SH/update.png ]"
  gui WINSCP_UPDATE_URL="http://127.0.0.1:9/x" WINSCP_UITEST="wait|1500;checkupdate;wait|2500"
  chk "update: manual check that fails says so and nothing more" "[ $RC -eq 0 ] && ghas 'Could not check'"
  gui WINSCP_UITEST="wait|2500"
  chk "update: scripted runs never go to the network by themselves" "[ $RC -eq 0 ] && ! ghas '\\[update\\]'"

  if [ -n "$IP" ]; then
    proxylog socks; rm -rf /tmp/dlq
    echo "gui tunnel" > "$DATA/sshhome/gui-a.txt"; head -c 5000000 /dev/urandom > "$DATA/sshhome/gui-b.bin"; mkdir -p "$DATA/sshhome/gui-dir/sub"; echo x > "$DATA/sshhome/gui-dir/sub/x.txt"
    gui WINSCP_DIALOG_SHOTS=$SH WINSCP_LOGIN_SAVE_AS=e2e-tunnel WINSCP_AUTOCONNECT=freescp-e2e-sshd:2222:winscp:winscp123:sftp \
        WINSCP_AC_TUNNEL=$IP:2224:jumper:jump456 WINSCP_AC_PROXY=socks5:127.0.0.1:11080 WINSCP_UITEST="loginpreset"
    sleep 2
    gui WINSCP_AUTOCONNECT_SITE=e2e-tunnel WINSCP_AUTOXFER=gui-a.txt,gui-b.bin,gui-dir WINSCP_UITEST="waitidle;print|left"
    chk "a site saved from the Login form (tunnel + SOCKS5 proxy) connects and downloads 3 items over parallel connections" \
        "[ $RC -eq 0 ] && cmp -s $DATA/sshhome/gui-b.bin /tmp/dlq/gui-b.bin && diff -r $DATA/sshhome/gui-dir /tmp/dlq/gui-dir >/dev/null && [ \$(grep -c 'socks5 $IP:2224' $DATA/run/socks.log) -ge 2 ]"
  else skip "saved site with tunnel + proxy (no non-localhost address)"; fi
  gui WINSCP_AUTOCONNECT_SITE=e2e-nope WINSCP_UITEST="wait|50";            chk "unknown saved site -> reported" "ghas 'no saved site named'"
  gui $AC WINSCP_UITEST="nav|right|/root;print|right;nav|right|/e2e-no-such;print|right"
  chk "unreadable / missing directory: an error dialog, the panel stays where it was" "ghas 'Error listing directory' && ghas 'Error changing directory' && [ \$(grep -c 'panel right\] /config\$' $GO) = 2 ]"
  if [ "${E2E_HAVE_KBDINT:-0}" = 1 ]; then
    gui WINSCP_AUTOCONNECT=127.0.0.1:2231:twofa:pw-secret:sftp "WINSCP_PROMPT_ANSWERS=pw-secret|246810" WINSCP_UITEST="print|right"
    chk "2FA login from the GUI (answers typed into the prompt)" "ghas 'panel right\] /' && ghas 'hello.txt'"
    gui WINSCP_DIALOG_SHOTS=$SH WINSCP_AUTOCONNECT=127.0.0.1:2231:twofa:pw-secret:sftp WINSCP_UITEST="wait|50"
    chk "the 2FA prompt dialog renders; cancelling it ends the login cleanly" "[ -s $SH/authprompt.png ] && ghas 'autoconnect\] failed'"
  else skip "2FA from the GUI (asyncssh)"; fi

  # transfer settings: remembered across runs, used by a transfer started from the panels
  H="$DATA/sshhome/e2e-g"; rm -rf "$H" "$W/gl"; mkdir -p "$H" "$W/gl"
  printf 'a\nb\n' > "$W/gl/note.txt"; head -c 2000 /dev/urandom > "$W/gl/img.bin"; echo x > "$W/gl/skip.tmp"
  gui WINSCP_UITEST="setxfer|mode=auto|speed=250|mask=^ *.tmp|rights=640;printxfer"
  gui WINSCP_UITEST="printxfer"
  chk "transfer settings are remembered across restarts" "ghas 'Automatic text/binary' && ghas '250 KB/s' && ghas '640'"
  gui $AC WINSCP_AC_CRLF=1 WINSCP_UITEST="nav|left|$W/gl;nav|right|/config/e2e-g;select|left|*;xfer|left;waitidle"
  chk "an upload from the panels honours them (text for .txt, mask, permissions)" \
      "[ \"\$(xxd -p $H/note.txt)\" = 610d0a620d0a ] && cmp -s $W/gl/img.bin $H/img.bin && [ ! -e $H/skip.tmp ] && [ \"\$(perm $H/note.txt)\" = 640 ]"
  gui $AC WINSCP_DIALOG_SHOTS=$SH WINSCP_UITEST="nav|left|$W/gl;select|left|*;copy|left"; chk "Copy dialog renders with the transfer settings line" "[ -s $SH/copy.png ]"

  # find / sizes
  mktree; rm -rf "$DATA/sshhome/e2e-tree"; cp -R "$T" "$DATA/sshhome/e2e-tree"; sleep 2
  gui WINSCP_DIALOG_SHOTS=$SH WINSCP_UITEST="nav|left|$T;find|left|*.txt";          chk "Find, local: 4 results" "ghas '\[find\] 4 result' && ghas 'sub/deep/e.txt'"
  gui $AC WINSCP_UITEST="nav|right|/config/e2e-tree;find|right|*.bin";                  chk "Find, remote: 1 result" "ghas '\[find\] 1 result' && ghas '/config/e2e-tree/sub/d.bin'"
  gui $AC WINSCP_FIND_FOCUS=1 WINSCP_UITEST="nav|right|/config/e2e-tree;find|right|e.txt;print|right"
  chk "Find > Focus: the panel shows the file's directory with the file selected" "ghas 'panel right\] /config/e2e-tree/sub/deep' && ghas '^  \* e.txt'"
  gui $AC WINSCP_UITEST="nav|right|/config/e2e-tree;find|right|*>12zz";                 chk "Find: invalid mask reported" "[ $RC -eq 0 ] && ghas 'invalid mask'"
  gui WINSCP_UITEST="nav|left|$T;dirsizes|left;waitidle;print|left";                chk "Directory sizes, local" "ghas 'sub/ | 1.0 KiB' && ghas 'sp ace/ | 7 B' && ghas 'empty/ | 0 B'"
  gui $AC WINSCP_UITEST="nav|right|/config/e2e-tree;dirsizes|right;waitidle;print|right"; chk "Directory sizes, remote" "ghas 'sub/ | 1.0 KiB' && ghas 'sp ace/ | 7 B' && ghas 'empty/ | 0 B'"
  gui $AC WINSCP_UITEST="nav|right|/config/e2e-tree;select|right|sub;dirsizes|right;waitidle;print|right"; chk "Directory sizes: only the selected directory" "ghas 'sub/ | 1.0 KiB' && ghas 'empty/ |  |'"

  # compare directories
  CL="$W/cmp"; CR="$DATA/sshhome/e2e-cmp"; rm -rf "$CL" "$CR"; mkdir -p "$CL/dir-l" "$CR/dir-r"
  echo same > "$CL/same.txt"; cp -p "$CL/same.txt" "$CR/same.txt"
  echo l > "$CL/only-left.txt"; echo r > "$CR/only-right.txt"
  echo aaa > "$CL/newer-left.txt"; echo aaa > "$CR/newer-left.txt"; touch -t 202601010000 "$CR/newer-left.txt"; touch -t 202606010000 "$CL/newer-left.txt"
  echo bbb > "$CL/newer-right.txt"; echo bbb > "$CR/newer-right.txt"; touch -t 202601010000 "$CL/newer-right.txt"; touch -t 202606010000 "$CR/newer-right.txt"
  echo short > "$CL/size.txt"; echo muchlonger > "$CR/size.txt"; touch -t 202603030303 "$CL/size.txt" "$CR/size.txt"; sleep 2
  gui $AC WINSCP_UITEST="nav|left|$CL;nav|right|/config/e2e-cmp;comparedirs|time;print|left;print|right;shot|$SH/comparedirs.png"
  chk "Compare Directories by time: [$(marked left)] / [$(marked right)]" "[ \"\$(marked left)\" = 'newer-left.txt,only-left.txt,' ] && [ \"\$(marked right)\" = 'newer-right.txt,only-right.txt,' ]"
  gui $AC WINSCP_UITEST="nav|left|$CL;nav|right|/config/e2e-cmp;comparedirs|size;print|left;print|right"
  chk "Compare Directories by size" "[ \"\$(marked left)\" = 'only-left.txt,size.txt,' ] && [ \"\$(marked right)\" = 'only-right.txt,size.txt,' ]"
  rm -rf "$W/same" "$DATA/sshhome/e2e-same"; mkdir -p "$W/same" "$DATA/sshhome/e2e-same"; echo x > "$W/same/a.txt"; cp -p "$W/same/a.txt" "$DATA/sshhome/e2e-same/a.txt"; sleep 2
  gui $AC WINSCP_UITEST="nav|left|$W/same;nav|right|/config/e2e-same;comparedirs|time|size"
  chk "Compare Directories: identical directories -> 'No differences found'" "ghas 'No differences found'"

  # compare files
  FL="$W/diff"; FR="$DATA/sshhome/e2e-diff"; rm -rf "$FL" "$FR"; mkdir -p "$FL" "$FR"
  printf 'alpha\nbeta\ngamma\ndelta\nepsilon\n' > "$FL/doc.txt"; printf 'alpha\nBETA changed\ngamma\nepsilon\nzeta added\n' > "$FR/doc.txt"
  cp "$FL/doc.txt" "$FL/same.txt"; cp "$FL/doc.txt" "$FR/same.txt"
  head -c 3000 /dev/urandom > "$FL/blob.bin"; head -c 3000 /dev/urandom > "$FR/blob.bin"
  printf 'one\ntwo\n' > "$FL/eol.txt"; printf 'one\r\ntwo\r\n' > "$FR/eol.txt"
  echo solo > "$FL/solo.txt"; echo other > "$FR/other-name.txt"; sleep 2
  D0="nav|left|$FL;nav|right|/config/e2e-diff"
  gui $AC WINSCP_DIALOG_SHOTS=$SH WINSCP_UITEST="$D0;select|left|doc.txt;comparefiles|left"
  chk "Compare Files, local vs remote: 3 differences, window rendered" "ghas 'The files are different: 3 differences (5 / 5 lines)' && [ -s $SH/diff.png ]"
  gui $AC WINSCP_UITEST="$D0;select|right|doc.txt;comparefiles|right";               chk "Compare Files started from the remote panel" "ghas 'different: 3 differences'"
  gui $AC WINSCP_UITEST="$D0;select|left|same.txt;comparefiles|left";                chk "Compare Files: identical" "ghas 'The files are identical'"
  gui $AC WINSCP_UITEST="$D0;select|left|blob.bin;comparefiles|left";                chk "Compare Files: binary files are not shown as text" "ghas 'different (binary files'"
  gui $AC WINSCP_UITEST="$D0;select|left|eol.txt;comparefiles|left";                 chk "Compare Files: LF vs CR/LF = no differing lines" "ghas 'different: 0 differences'"
  gui $AC WINSCP_UITEST="$D0;select|left|solo.txt;select|right|other-name.txt;comparefiles|left"; chk "Compare Files: different names, one selected in each panel" "ghas 'different: 1 difference '"
  gui $AC WINSCP_UITEST="nav|left|$FL;nav|right|/config;select|left|solo.txt;select|right|e2e-nothing;comparefiles|left"; chk "Compare Files: no counterpart is explained" "ghas 'has no counterpart'"
  gui $AC WINSCP_UITEST="setxfer|defaults=1|mask=*.jpg|case=upper;$D0;select|left|doc.txt;comparefiles|left;setxfer|defaults=1"
  chk "Compare Files is not affected by the transfer settings (mask, name case)" "ghas 'different: 3 differences'"
  chk "no temporary comparison copies left behind" "[ \"\$(ls /tmp/freescp-compare 2>/dev/null | wc -l | tr -d ' ')\" = 0 ] || [ ! -d \"\${TMPDIR:-/tmp}/freescp-compare\" ] || [ \"\$(ls \"\${TMPDIR:-/tmp}/freescp-compare\" | wc -l | tr -d ' ')\" = 0 ]"
  unset QT_QPA_PLATFORM WINSCP_NO_AUTOLOGIN
  echo "  (screenshots: $SH)"
fi

#==============================================================================================
if want ops; then
  echo "== ops: capabilities and session information"
  OPS="$W/ops"; rm -rf "$OPS"; mkdir -p "$OPS"
  echo "alpha content" > "$OPS/a.txt"; echo "beta ñ" > "$OPS/b ñ.txt"; head -c 70000 /dev/urandom > "$OPS/c.bin"
  for e in "${PROTOS[@]}"; do
    IFS='|' read -r name opts base host <<< "$e"
    case $name in
      sftp) caps='chmod=1 chown=1 chgrp=1 symlink=1 hardlink=1 copy=1 move=1 space=1'; proto='SFTP-'; enc=1;;
      scp)  caps='chmod=1 chown=1 chgrp=1 symlink=1 hardlink=1 copy=1 move=1 space=0'; proto='SCP';   enc=1;;
      ftp)  caps='chmod=1 chown=0 chgrp=0 symlink=0 hardlink=0 copy=0 move=1 space=0'; proto='FTP';   enc=0;;
      dav)  caps='chmod=0 chown=0 chgrp=0 symlink=0 hardlink=0 copy=1 move=1 space=1'; proto='WebDAV'; enc=0;;
      s3)   caps='chmod=0 chown=0 chgrp=0 symlink=0 hardlink=0 copy=1 move=1 space=0'; proto='S3';    enc=0;;
    esac
    run $opts -- caps info
    chk "$name: capabilities ($caps)" "[ $RC -eq 0 ] && has 'CAPS $caps'"
    chk "$name: session information names the protocol; encrypted=$enc" "has \"INFO protocol='$proto\" && has 'encrypted=$enc' && has 'S File transfer protocol = $proto'"
  done
  run port=2121 user=winscp pass=winscp123 proto=ftp tls=1 -- info
  chk "ftps: session information reports the TLS connection as encrypted" "[ $RC -eq 0 ] && has 'encrypted=1' && has 'S Cryptographic protocol = .*TLS'"
  run $SSH -- info
  chk "sftp: cipher, SSH implementation and host key fingerprint are reported" "has 'S Encryption algorithm' && has 'S SSH implementation = OpenSSH' && has 'S Server host key fingerprint (SHA-256) = '"

  for e in "${PROTOS[@]}"; do
    IFS='|' read -r name opts base host <<< "$e"
    echo "== ops: $name"
    d="$base/e2e-ops"; b="${base:-/}"
    # is the server's directory a plain directory on this machine? (S3: MinIO keeps its own layout)
    hd=""; [ "$name" != s3 ] && hd="$host/e2e-ops"
    tmo 60 "$C" $opts -- "ls|$b" "rm|e2e-ops" > /dev/null 2>&1      # left over from an earlier run

    # --- directories: create, delete empty, delete with contents (S3 sent "dir//" and failed) ---
    run $opts -- "ls|$b" "mkdir|e2e-ops" "ls|$d" "mkdir|empty" "mkdir|full" "ls|$d/full" "put|$OPS/a.txt|$d/full" "mkdir|inner" \
                 "put|$OPS/b ñ.txt|$d/full/inner" "ls|$d" "rm|empty" "rm|full" "lsl|$d"
    chk "$name: delete an empty directory and a directory with contents" "[ $RC -eq 0 ] && has 'OK   rm|empty' && has 'OK   rm|full' && ! has '^  L '"

    # --- a small tree for the rest ---
    run $opts -- "ls|$d" "put|$OPS/a.txt|$d" "put|$OPS/c.bin|$d" "mkdir|tree" "put|$OPS/a.txt|$d/tree" "ls|$d/tree" "mkdir|sub" "put|$OPS/b ñ.txt|$d/tree/sub"
    chk "$name: test tree created" "[ $RC -eq 0 ]"

    # --- permissions ---
    if [ $name = sftp ] || [ $name = scp ] || [ $name = ftp ]; then
      run $opts -- "ls|$d" "props|a.txt|rights=600" "lsl|$d"
      chk "$name: chmod 600" "[ $RC -eq 0 ] && has '  L - rw------- .* a.txt' && [ \"\$(perm $hd/a.txt)\" = 600 ]"
      run $opts -- "ls|$d" "props|tree|rights=640|recursive|addx" "lsl|$d" "lsl|$d/tree" "lsl|$d/tree/sub"
      chk "$name: recursive chmod 640, directories get x where r is set" \
          "[ $RC -eq 0 ] && has '  L d rwxr-x--- .* tree' && has '  L - rw-r----- .* a.txt' && has '  L d rwxr-x--- .* sub' && has '  L - rw-r----- .* b ñ.txt'"
      run $opts -- "ls|$d" "props|a.txt|rights=9z" "props|a.txt|rights=64" "props|e2e-nope|rights=600" "ls|$d"
      chk "$name: invalid permissions / unknown file are refused, the session keeps working" \
          "[ $RC -eq 2 ] && [ \$(grep -c 'Invalid permissions' $OUT) = 2 ] && has 'FAIL props|e2e-nope' && [ \$(grep -c '^OK   ls|' $OUT) = 2 ]"
    else
      run $opts -- "ls|$d" "props|a.txt|rights=600" "props|a.txt|owner=root" "ls|$d"
      chk "$name: permissions / owner cannot be changed: a clear refusal, the session keeps working" \
          "[ $RC -eq 2 ] && has 'does not support changing permissions' && has 'does not support changing the owner' && [ \$(grep -c '^OK   ls|' $OUT) = 2 ]"
    fi
    if [ $name = sftp ] || [ $name = scp ]; then
      # special bits, owner and group on the server's own file system (a bind mount from a Mac
      # drops setuid and accepts any chown)
      o="/tmp/e2e-own-$name"
      tmo 60 "$C" $opts -- "ls|/tmp" "rm|e2e-own-$name" > /dev/null 2>&1
      run $opts -- "ls|/tmp" "mkdir|e2e-own-$name" "put|$OPS/a.txt|$o" "put|$OPS/c.bin|$o" "ls|$o" "props|c.bin|rights=4755" "lsl|$o" "props|c.bin|rights=0644" "lsl|$o"
      chk "$name: special bits (4755 -> rwsr-xr-x) and a four-digit value" "[ $RC -eq 0 ] && has '  L - rwsr-xr-x .* c.bin' && has '  L - rw-r--r-- .* c.bin'"
      run $opts -- "ls|$o" "props|a.txt|group=1000" "lsl|$o"
      chk "$name: change the group by numeric id" "[ $RC -eq 0 ] && has '  L - .* winscp:users a.txt'"
      # by name: SCP runs chgrp with the name; SFTP-3 can only send ids, so the name must be one the
      # session knows (here: the group of the other file of the directory), otherwise a clear refusal
      G0="$(awk '/  L - .* c.bin/ { split($4, a, ":"); print a[2] }' "$OUT")"
      run $opts -- "ls|$o" "props|a.txt|group=$G0" "lsl|$o"
      chk "$name: change the group by name ($G0)" "[ $RC -eq 0 ] && [ -n \"$G0\" ] && has \"  L - .* winscp:$G0 a.txt\""
      if [ $name = sftp ]; then
        run $opts -- "ls|$o" "props|a.txt|group=e2e-nogroup" "props|a.txt|owner=e2e-nouser" "lsl|$o"
        chk "sftp: a name whose id is not known is refused (it is never sent as id 0)" "[ $RC -eq 2 ] && [ \$(grep -c 'by numeric id only' $OUT) = 2 ] && has \"  L - .* winscp:$G0 a.txt\""
      else
        run $opts -- "ls|$o" "props|a.txt|group=users" "lsl|$o"
        chk "scp: change the group by a name not seen in the listing" "[ $RC -eq 0 ] && has '  L - .* winscp:users a.txt'"
      fi
      run $opts -- "ls|$o" "props|a.txt|owner=root" "lsl|$o" "props|a.txt|owner=|group=" "ls|$o"
      chk "$name: changing the owner without the right to is an error, nothing changes, the session keeps working" \
          "[ $RC -eq 2 ] && has 'FAIL props|a.txt|owner=root' && has '  L - .* winscp:[a-z]* a.txt' && has 'cannot be empty' && [ \$(grep -c '^OK   ls|' $OUT) = 2 ]"
      tmo 60 "$C" $opts -- "ls|/tmp" "rm|e2e-own-$name" > /dev/null 2>&1
    fi

    # --- links ---
    if [ $name = sftp ] || [ $name = scp ]; then
      run $opts -- "ls|$d" "ln|lnk ñ|a.txt" "hardln|hard.txt|$d/a.txt" "lsl|$d"
      chk "$name: symbolic link (non-ASCII name) and hard link" \
          "[ $RC -eq 0 ] && has '  L l .* lnk ñ -> a.txt' && has '  L - .* hard.txt' && [ \"\$(readlink \"$hd/lnk ñ\")\" = a.txt ] && [ $hd/a.txt -ef $hd/hard.txt ]"
      run $opts -- "ls|$d" "ln|a.txt|c.bin" "ln||a.txt" "ln|x|" "ls|$d"
      chk "$name: a link over an existing name / without a name or target is refused" "[ $RC -eq 2 ] && [ \$(grep -c '^FAIL ln' $OUT) = 3 ] && [ \$(grep -c '^OK   ls|' $OUT) = 2 ]"
      freshdl; run $opts -- "getplain|$d/lnk ñ|$W/dl"
      chk "$name: a file is downloaded through its symbolic link" "[ $RC -eq 0 ] && cmp -s $OPS/a.txt \"$W/dl/lnk ñ\""
    else
      run $opts -- "ls|$d" "ln|lnk|a.txt" "hardln|hl|$d/a.txt" "ls|$d"
      chk "$name: links are not available: a clear refusal" "[ $RC -eq 2 ] && has 'does not support symbolic links' && has 'does not support hard links' && [ \$(grep -c '^OK   ls|' $OUT) = 2 ]"
    fi

    # --- duplicate (server-side copy) ---
    if [ $name = ftp ]; then
      run $opts -- "ls|$d" "dup|a.txt|$d|copy.txt" "lsl|$d"
      chk "ftp: duplicate is not available: a clear refusal, nothing created" "[ $RC -eq 2 ] && has 'does not support duplicating' && ! has ' copy.txt'"
    else
      freshdl
      run $opts -- "ls|$d" "dup|c.bin|$d|copy ñ.bin" "dup|a.txt|$d/tree/sub" "lsl|$d/tree/sub" "getplain|$d/copy ñ.bin|$W/dl"
      chk "$name: duplicate a file under a new (non-ASCII) name and into another directory, byte-identical" \
          "[ $RC -eq 0 ] && has '  L - .* a.txt' && cmp -s $OPS/c.bin \"$W/dl/copy ñ.bin\""
      if [ $name = s3 ]; then      # S3 has no directories to be missing: a new key prefix is simply created
        run $opts -- "ls|$d" "dup|a.txt|$d|a.txt" "dup|a.txt|" "lsl|$d"
        chk "s3: duplicate onto itself / nowhere is an error" "[ $RC -eq 2 ] && has 'same file' && [ \$(grep -c '^FAIL dup' $OUT) = 2 ]"
      else
        run $opts -- "ls|$d" "dup|a.txt|$d|a.txt" "dup|a.txt|$d/e2e-nope" "dup|a.txt|" "lsl|$d"
        chk "$name: duplicate onto itself / into a missing directory / nowhere is an error; nothing appears" \
            "[ $RC -eq 2 ] && has 'same file' && [ \$(grep -c '^FAIL dup' $OUT) = 3 ] && ! has ' e2e-nope'"
      fi
      if [ $name = scp ] || [ $name = dav ]; then
        run $opts -- "ls|$d" "dup|tree|$d|tree2" "lsl|$d/tree2" "lsl|$d/tree2/sub"
        chk "$name: duplicate a directory tree" "[ $RC -eq 0 ] && has '  L d .* sub' && has '  L - .* b ñ.txt'"
      else
        run $opts -- "ls|$d" "dup|tree|$d|tree2" "lsl|$d"
        chk "$name: a directory cannot be duplicated with this protocol: said so, nothing left behind" "[ $RC -eq 2 ] && has 'Duplicating a directory is not supported' && ! has ' tree2'"
      fi
    fi

    # --- move ---
    freshdl
    run $opts -- "ls|$d" "mv|c.bin|$d/tree|moved ñ.bin" "lsl|$d" "getplain|$d/tree/moved ñ.bin|$W/dl"
    chk "$name: move a file into another directory under a new name, byte-identical" \
        "[ $RC -eq 0 ] && ! has '  L - .* c.bin' && cmp -s $OPS/c.bin \"$W/dl/moved ñ.bin\""
    run $opts -- "ls|$d" "mv|tree|$d/tree|inside" "mv|tree|$d/tree/sub|inside" "mv|tree|$d/e2e-nope|x" "mv|e2e-nope|$d/tree" "lsl|$d" "lsl|$d/tree"
    chk "$name: a directory is not moved into itself or into a missing directory; it stays intact" \
        "[ $RC -eq 2 ] && [ \$(grep -c 'into itself' $OUT) = 2 ] && [ \$(grep -c '^FAIL mv' $OUT) = 4 ] && has '  L d .* tree' && has '  L d .* sub' && has 'moved ñ.bin'"
    if [ $name = s3 ]; then
      run $opts -- "ls|$d" "mv|tree|$d|tree moved" "rename|tree|tree renamed" "lsl|$d" "lsl|$d/tree"
      chk "s3: a folder cannot be moved or renamed: said so, it stays intact" "[ $RC -eq 2 ] && [ \$(grep -c 'folder is not supported on S3' $OUT) = 2 ] && has '  L d .* tree' && has 'moved ñ.bin'"
      run $opts -- "ls|$d" "rename|a.txt|a renamed ñ.txt" "lsl|$d"
      chk "s3: a file is renamed" "[ $RC -eq 0 ] && has 'a renamed ñ.txt'"
    else
      run $opts -- "ls|$d" "mv|tree|$d|tree moved" "lsl|$d" "lsl|$d/tree moved/sub"
      chk "$name: move (rename) a directory tree" "[ $RC -eq 0 ] && has '  L d .* tree moved' && has '  L - .* b ñ.txt'"
      run $opts -- "ls|$d" "rename|tree moved|tree ñ" "rename|a.txt|a renamed ñ.txt" "lsl|$d"
      chk "$name: rename a directory and a file" "[ $RC -eq 0 ] && has '  L d .* tree ñ' && has 'a renamed ñ.txt'"
    fi

    # --- free space ---
    run $opts -- "df|${base:-/}" "ls|$b"
    case $name in
      sftp) chk "sftp: free space of the file system" "[ $RC -eq 0 ] && has 'DF total=[1-9][0-9]* unused=[1-9]'";;
      dav)  chk "dav: free space request answered (this server reports no quota)" "[ $RC -eq 0 ] && has '^DF total='";;
      *)    chk "$name: free space is not available: said so, the session keeps working" "[ $RC -eq 2 ] && has 'does not tell how much space' && has '^OK   ls|'";;
    esac

    # --- a lost connection is noticed and the session reopened in place ---
    run $opts -- "ls|$b" lost drop lost "ls|$b" reconnect lost "ls|$b" "mkdir|e2e-ops-rc" "ls|$b" "rm|e2e-ops-rc"
    chk "$name: reconnect a session whose connection is gone" \
        "[ $RC -eq 2 ] && has 'LOST 0 connected=1' && has 'LOST 1 connected=0' && has 'RECONNECT ok=1 .* sessions=1 active=0' && has 'OK   mkdir|e2e-ops-rc' && has 'OK   rm|e2e-ops-rc'"

    run $opts -- "ls|$b" "rm|e2e-ops" "ls|$b"
    chk "$name: the whole test tree is deleted" "[ $RC -eq 0 ] && [ \$(grep -c '^  d .* e2e-ops  ' $OUT) = 1 ]"
  done
fi

#==============================================================================================
if want panels; then
  echo "== panels (offscreen): layout"
  SH="$W/shots"; GO="$W/gui.out"; mkdir -p "$SH"
  export QT_QPA_PLATFORM=offscreen WINSCP_NO_AUTOLOGIN=1
  AC="WINSCP_AUTOCONNECT=127.0.0.1:2223:winscp:winscp123:sftp"
  gui() { tmo 120 env "$@" "$APP" > "$GO" 2>&1; RC=$?; cp "$GO" "$OUT"; }
  ghas() { grep -aq -- "$1" "$GO"; }
  gcount() { grep -ac -- "$1" "$GO"; }
  marked() { awk -v side="$1" '$0 ~ "panel " side {p=1; next} /^\[panel/{p=0} p && /^  \*/{print $2}' "$GO" | tr '\n' ','; }
  names() { awk -v side="$1" '$0 ~ "panel " side {p=1; next} /^\[/{p=0} p { if ($1 == "*") print $2; else print $1 }' "$GO" | tr '\n' ','; }
  # the Changed cell of a row of a "columns|<side>" block (field number differs: local has a Type column)
  cell() { awk -F' [|] ' -v side="$1" -v row="  $2" -v f="$3" '$0 ~ "^\\[columns " side {p=1; next} /^\[/{p=0} p && $1 == row {print $f}' "$GO"; }
  defaults delete com.winscp-native-port-test.WinSCP >/dev/null 2>&1; rm -rf "$HOME/.config/WinSCP-native-port-test" 2>/dev/null

  PL="$W/pl"; rm -rf "$PL"; mkdir -p "$PL/dirA" "$PL/dir.txt"
  echo one > "$PL/a.txt"; echo two22 > "$PL/b.txt"; head -c 3000 /dev/zero > "$PL/big.bin"; echo h > "$PL/.hid"; echo m > "$PL/c.md"; echo k > "$PL/old.bak"
  ln -s a.txt "$PL/lnk"; chmod 644 "$PL/a.txt"; touch -t 202601021530.45 "$PL/a.txt"
  RP=/config/e2e-pn; HP="$DATA/sshhome/e2e-pn"; rm -rf "$HP"; mkdir -p "$HP/sub/deep"
  echo r1 > "$HP/r1.txt"; echo r2 > "$HP/r2.log"; echo n > "$HP/sub/n.txt"; echo d > "$HP/sub/deep/d.txt"; ln -s r1.txt "$HP/rl"
  chmod 644 "$HP/r1.txt" "$HP/r2.log" "$HP/sub/n.txt" "$HP/sub/deep/d.txt"; chmod 755 "$HP" "$HP/sub" "$HP/sub/deep"
  N="nav|left|$PL;nav|right|$RP"

  gui $AC WINSCP_UITEST="$N;columns|left;columns|right;status|left;status|right;mainstatus"
  chk "columns: local = Name, Size, Type, Changed; remote = Name, Size, Changed, Rights, Owner" \
      "[ $RC -eq 0 ] && ghas '^  Name | Size | Type | Changed\$' && ghas '^  Name | Size | Changed | Rights | Owner\$'"
  chk "Type column: parent row, folders, links, files" \
      "ghas '^  \\.\\. |  | Parent directory | \$' && ghas '^  dirA |  | File folder | ' && ghas '^  lnk | .* | Symbolic link | ' && ghas '^  big.bin | .* | [A-Za-z].* | '"
  chk "remote rows show size, rights and owner; the link is a link" "ghas '^  r1.txt | 3 B | .* | rw-r--r-- | winscp\$' && [ -n \"\$(cell right rl 3)\" ]"
  chk "status lines: '<selected> of <total> in <n> of <count>'" "ghas '^\\[status left\\] 0 B of .* in 0 of 9 || \$' && ghas '^\\[status right\\] 0 B of .* in 0 of 4 || \$'"
  chk "main status bar: protocol, lock, session time" "ghas \"^\\[mainstatus\\] protocol='SFTP-[0-9]*' encrypted=1 time='0:00:0[0-9]'\""

  gui $AC WINSCP_UITEST="$N;select|left|a.txt;xfer|left;waitidle;columns|left;columns|right"
  LT="$(cell left a.txt 4)"; RT="$(cell right a.txt 3)"
  chk "a timestamp reads the same in the local and the remote panel ($LT)" "[ -n \"\$LT\" ] && [ \"\$LT\" = \"\$RT\" ] && echo \"\$LT\" | grep -q '2026.*30.45\\|2026.*30:45'"
  rm -f "$HP/a.txt"

  gui $AC WINSCP_UITEST="closetab|0;mainstatus;columns|right"
  chk "after the session is closed the right panel is a local panel again and the status bar is empty" \
      "ghas \"^\\[mainstatus\\] protocol='' encrypted=0 time=''\" && ghas '^  Name | Size | Type | Changed\$' && ! ghas 'Rights'"
  gui WINSCP_AUTOCONNECT=127.0.0.1:21:winscp:winscp123:ftp WINSCP_UITEST="mainstatus;columns|right"
  chk "ftp session: status bar says FTP, not encrypted; remote columns" "ghas \"protocol='FTP' encrypted=0\" && ghas '^  Name | Size | Changed | Rights | Owner\$'"
  gui WINSCP_AUTOCONNECT=127.0.0.1:2121:winscp:winscp123:ftp WINSCP_AC_TLS=explicit WINSCP_UITEST="mainstatus"
  chk "ftps session: status bar shows it as encrypted" "ghas \"protocol='FTP' encrypted=1\""
  gui WINSCP_AUTOCONNECT=127.0.0.1:8087:winscp:winscp123:dav WINSCP_UITEST="mainstatus;columns|right;status|right"
  chk "webdav session: status bar, columns and status line" "ghas \"protocol='WebDAV' encrypted=0\" && ghas '^  Name | Size | Changed | Rights | Owner\$' && ghas '^\\[status right\\] 0 B of '"
  gui WINSCP_AUTOCONNECT=127.0.0.1:9101:minioadmin:minioadmin123:s3 WINSCP_UITEST="nav|right|/testbucket;mkdir|right|e2e-gdel;print|right;select|right|e2e-gdel;delete|right;print|right;mainstatus"
  chk "s3 session: a directory created in the panel can be deleted from it" "ghas \"protocol='S3'\" && [ \$(gcount '^    e2e-gdel/ \\|^  \\* e2e-gdel/ ') = 1 ]"

  echo "== panels: hidden files, filter"
  gui WINSCP_UITEST="nav|left|$PL;hidden|0;print|left;status|left;hidden|1;print|left;status|left"
  chk "hidden files off: gone from the list, counted on the status line; on again: back" \
      "[ \$(gcount '\\.hid |') = 1 ] && ghas '^\\[status left\\] 0 B of .* in 0 of 8 || 1 hidden\$' && ghas '^\\[status left\\] 0 B of .* in 0 of 9 || \$'"
  gui WINSCP_UITEST="nav|left|$PL;filter|left|*.txt;print|left;status|left"
  chk "filter *.txt: matching files and all directories stay" "[ \"\$(names left)\" = 'dir.txt/,dirA/,a.txt,b.txt,' ] && ghas '|| 5 filtered (\\*.txt)\$'"
  # in a scripted command ';' separates the steps and '|' the arguments: a mask list uses ',' and '^' stands for '|'
  gui WINSCP_UITEST="nav|left|$PL;filter|left|*^*.bak, *.md;print|left;filter|left;print|left;status|left"
  chk "filter with an exclude part; removing the filter shows everything again" \
      "[ \$(gcount 'old.bak |') = 1 ] && [ \$(gcount 'c.md |') = 1 ] && [ \$(gcount 'big.bin |') = 2 ] && ghas '^\\[status left\\] .* in 0 of 9 || \$'"
  gui WINSCP_UITEST="nav|left|$PL;filter|left|*>abc;print|left"
  chk "an invalid filter mask is refused and filters nothing" "ghas '^\\[filter\\] invalid' && ghas 'big.bin |' && ghas 'old.bak |'"
  gui WINSCP_DIALOG_SHOTS=$SH WINSCP_DIALOG_SET="filter.mask=*.md" WINSCP_DIALOG_ACCEPT=filter WINSCP_UITEST="nav|left|$PL;filter|left|dialog;print|left"
  chk "Filter dialog" "[ -s $SH/filter.png ] && [ \"\$(names left)\" = 'dir.txt/,dirA/,c.md,' ]"
  gui WINSCP_DIALOG_SET="filter.mask=*>abc" WINSCP_DIALOG_ACCEPT=filter WINSCP_UITEST="nav|left|$PL;filter|left|dialog;status|left"
  chk "Filter dialog refuses an invalid mask" "ghas 'The file mask is not valid' && ghas ' in 0 of 9 || \$'"
  gui $AC WINSCP_UITEST="$N;filter|right|*.log;print|right;status|right"
  chk "filter on the remote panel" "[ \"\$(names right)\" = 'sub/,r2.log,' ] && ghas '|| 2 filtered (\\*.log)\$'"

  echo "== panels: selection, sorting"
  gui WINSCP_UITEST="nav|left|$PL;clearsel|left;selectmask|left|*.txt;print|left;status|left"
  chk "select by mask: files only" "[ \"\$(marked left)\" = 'a.txt,b.txt,' ] && ghas ' in 2 of 9 || \$'"
  gui WINSCP_UITEST="nav|left|$PL;clearsel|left;selectmask|left|*.txt|dirs;print|left"
  chk "select by mask applied to directories too" "[ \"\$(marked left)\" = 'dir.txt/,a.txt,b.txt,' ]"
  gui WINSCP_UITEST="nav|left|$PL;clearsel|left;selectmask|left|*.md, *.bak;print|left;selectmask|left|*>abc"
  chk "select by a mask list; an invalid mask is reported" "[ \"\$(marked left)\" = 'c.md,old.bak,' ] && ghas '^\\[selectmask\\] invalid mask'"
  gui WINSCP_UITEST="nav|left|$PL;selectall|left;status|left;invert|left;status|left;selectmask|left|*.txt;invert|left;print|left"
  chk "select all (never the parent row), invert" "ghas ' in 9 of 9 || \$' && ghas ' in 0 of 9 || \$' && [ \"\$(marked left)\" = 'dir.txt/,dirA/,.hid,big.bin,c.md,lnk,old.bak,' ]"
  gui WINSCP_DIALOG_SHOTS=$SH WINSCP_DIALOG_SET="select.mask=*.md;select.dirs=0" WINSCP_DIALOG_ACCEPT=select WINSCP_UITEST="nav|left|$PL;clearsel|left;selectdialog|left;print|left"
  chk "Select dialog" "[ -s $SH/select.png ] && [ \"\$(marked left)\" = 'c.md,' ]"
  gui WINSCP_UITEST="nav|left|$PL;sort|left|1;print|left"
  S1="$(names left)"
  gui WINSCP_UITEST="nav|left|$PL;sort|left|1;sort|left|1;print|left"
  S2="$(names left)"
  chk "sort by size: directories stay first, ascending then descending ($S2)" \
      "echo \"\$S1\" | grep -q '^dir.txt/,dirA/,.*,big.bin,\$' && echo \"\$S2\" | grep -q '^dirA/,dir.txt/,big.bin,'"
  gui WINSCP_UITEST="nav|left|$PL;sort|left|3;print|left"
  chk "sort by the Changed column (the oldest file first)" "[ \"\$(names left | cut -d, -f3)\" = a.txt ]"

  echo "== panels: navigation, recent directories, bookmarks"
  gui WINSCP_UITEST="nav|left|$PL;nav|left|$PL/dirA;up|left;back|left;print|left;recent|left;forward|left;print|left"
  chk "up / back / forward" "ghas '^\\[panel left\\] $PL/dirA\$' && ghas '^\\[panel left\\] $PL\$'"
  chk "recent directories, most recent first; the drop-down lists the path from the root" \
      "ghas '^\\[recent left\\] $PL/dirA , $PL' && ghas '^\\[dircombo left\\] / , /tmp , /tmp/freescp-e2e , $PL , $PL/dirA , '"
  gui WINSCP_UITEST="recent|left"
  chk "recent directories are remembered by the next start" "ghas '^\\[recent left\\] .*$PL/dirA'"
  gui WINSCP_UITEST="nav|left|$PL;nav|left|$PL/e2e-nope;print|left"
  chk "a local directory that does not exist: an error, the panel stays where it was" "ghas 'does not exist' && ghas '^\\[panel left\\] $PL\$'"
  gui WINSCP_UITEST="nav|left|$PL//dirA/;print|left"
  chk "a typed path is tidied (repeated / trailing slashes)" "ghas '^\\[panel left\\] $PL/dirA\$'"
  gui WINSCP_DIALOG_SHOTS=$SH WINSCP_DIALOG_SET="opendir.path=$PL/dirA" WINSCP_DIALOG_ACCEPT=opendir WINSCP_UITEST="nav|left|$PL;opendir|left;print|left"
  chk "Open Directory dialog" "[ -s $SH/opendir.png ] && ghas '^\\[panel left\\] $PL/dirA\$'"
  gui $AC WINSCP_DIALOG_SET="opendir.path=$RP/sub/deep" WINSCP_DIALOG_ACCEPT=opendir WINSCP_UITEST="opendir|right;print|right"
  chk "Open Directory on the remote panel" "ghas '^\\[panel right\\] $RP/sub/deep\$' && ghas 'd.txt |'"
  gui WINSCP_UITEST="nav|left|$PL;bookmark|left;nav|left|$PL/dirA;bookmark|left;bookmark|left;bookmarks"
  chk "bookmarks: added once each" "ghas '^\\[bookmarks\\] $PL , $PL/dirA\$'"

  echo "== panels: Properties"
  PA="WINSCP_DIALOG_ACCEPT=properties"
  gui $AC WINSCP_DIALOG_SHOTS=$SH WINSCP_DIALOG_SET="properties.octal=600" $PA WINSCP_UITEST="$N;select|right|r1.txt;props|right"
  chk "Properties dialog: permissions of a remote file" "[ -s $SH/properties.png ] && [ \"\$(perm $HP/r1.txt)\" = 600 ]"
  gui $AC WINSCP_DIALOG_SET="properties.octal=9z" $PA WINSCP_UITEST="$N;select|right|r1.txt;props|right"
  chk "Properties dialog refuses an invalid value; nothing changes" "ghas 'three or four octal digits' && [ \"\$(perm $HP/r1.txt)\" = 600 ]"
  gui $AC $PA WINSCP_UITEST="$N;select|right|r2.log;props|right"
  chk "Properties accepted without a change does nothing" "[ $RC -eq 0 ] && [ \"\$(perm $HP/r2.log)\" = 644 ] && ! ghas 'could not be changed'"
  gui $AC WINSCP_DIALOG_SET="properties.octal=640" $PA WINSCP_UITEST="$N;select|right|r1.txt, r2.log;props|right"
  chk "Properties of two files at once" "[ \"\$(perm $HP/r1.txt)\" = 640 ] && [ \"\$(perm $HP/r2.log)\" = 640 ]"
  gui $AC WINSCP_DIALOG_SET="properties.octal=640;properties.recursive=1;properties.addx=1" $PA WINSCP_UITEST="$N;select|right|sub;props|right"
  chk "Properties of a directory, recursively, adding x to directories" \
      "[ \"\$(perm $HP/sub)\" = 750 ] && [ \"\$(perm $HP/sub/n.txt)\" = 640 ] && [ \"\$(perm $HP/sub/deep)\" = 750 ] && [ \"\$(perm $HP/sub/deep/d.txt)\" = 640 ]"
  gui $AC WINSCP_DIALOG_SET="properties.octal=700" $PA WINSCP_UITEST="$N;select|right|sub;props|right"
  chk "Properties of a directory, not recursively: its contents are left alone" "[ \"\$(perm $HP/sub)\" = 700 ] && [ \"\$(perm $HP/sub/n.txt)\" = 640 ]"
  chmod 755 "$HP/sub"
  gui $AC WINSCP_DIALOG_SET="properties.group=1000" $PA WINSCP_UITEST="nav|right|/tmp;mkdir|right|e2e-gp;select|right|e2e-gp;props|right;select|right|e2e-gp;props|right;select|right|e2e-gp;delete|right"
  chk "Properties dialog: change the group (numeric id)" "ghas '^\\[mkdir\\] ok' && ! ghas 'could not be changed'"
  gui $AC WINSCP_DIALOG_SET="properties.group=e2e-nogroup" $PA WINSCP_UITEST="nav|right|/tmp;mkdir|right|e2e-gq;select|right|e2e-gq;props|right;select|right|e2e-gq;delete|right"
  chk "Properties dialog: a group name the SFTP session cannot translate is refused with an explanation" "ghas 'by numeric id only'"
  gui $AC WINSCP_DIALOG_SET="properties.owner=root" $PA WINSCP_UITEST="nav|right|/tmp;mkdir|right|e2e-ow;select|right|e2e-ow;props|right;select|right|e2e-ow;delete|right;print|right"
  chk "Properties dialog: a refused owner change is reported" "ghas 'could not be changed' && ! ghas 'e2e-ow/'"
  gui WINSCP_DIALOG_SET="properties.octal=600" $PA WINSCP_UITEST="nav|left|$PL;select|left|b.txt;props|left"
  chk "Properties of a local file" "[ \"\$(perm $PL/b.txt)\" = 600 ]"
  gui WINSCP_AUTOCONNECT=127.0.0.1:8087:winscp:winscp123:dav WINSCP_DIALOG_SHOTS=$SH WINSCP_DIALOG_SET="properties.octal=600" $PA WINSCP_UITEST="mkdir|right|e2e-pp;select|right|e2e-pp;props|right;select|right|e2e-pp;delete|right;print|right"
  chk "Properties on a server without permissions (WebDAV): nothing is sent, no error" "[ $RC -eq 0 ] && ! ghas 'could not be changed' && ! ghas 'e2e-pp/'"

  echo "== panels: links, duplicate, move"
  gui $AC WINSCP_DIALOG_SHOTS=$SH WINSCP_DIALOG_SET="link.name=nl ñ" WINSCP_DIALOG_ACCEPT=link WINSCP_UITEST="$N;select|right|r1.txt;newlink|right;columns|right"
  chk "Add Link dialog: a symbolic link to the selected remote file" "[ -s $SH/link.png ] && [ \"\$(readlink \"$HP/nl ñ\")\" = r1.txt ] && ghas '^  nl ñ | '"
  gui $AC WINSCP_DIALOG_ACCEPT=link WINSCP_UITEST="$N;select|right|r1.txt;newlink|right"
  chk "Add Link dialog refuses an empty name" "ghas 'Enter the name of the link'"
  gui $AC WINSCP_DIALOG_SET="link.name=r2.log" WINSCP_DIALOG_ACCEPT=link WINSCP_UITEST="$N;select|right|r1.txt;newlink|right"
  chk "a link over an existing file is an error" "ghas 'Error creating link' && [ ! -L $HP/r2.log ]"
  gui $AC WINSCP_UITEST="$N;mklink|right|hl|$RP/r1.txt|hard"
  chk "hard link on the server" "[ $HP/hl -ef $HP/r1.txt ]"
  gui WINSCP_DIALOG_SET="link.name=ll" WINSCP_DIALOG_ACCEPT=link WINSCP_UITEST="nav|left|$PL;select|left|a.txt;newlink|left;columns|left"
  chk "a symbolic link in the local panel" "[ \"\$(readlink $PL/ll)\" = a.txt ] && ghas '^  ll | .* | Symbolic link | '"
  gui WINSCP_UITEST="nav|left|$PL;mklink|left|lh|a.txt|hard;mklink|left|a.txt|b.txt"
  chk "a local hard link; a link over an existing name is an error" "[ $PL/lh -ef $PL/a.txt ] && ghas 'Error creating link'"

  gui $AC WINSCP_DIALOG_SHOTS=$SH WINSCP_DIALOG_SET="duplicate.name=r1 copy ñ.txt" WINSCP_DIALOG_ACCEPT=duplicate WINSCP_UITEST="$N;select|right|r1.txt;duplicate|right;print|right"
  chk "Duplicate dialog: a remote file under a new name" "[ -s $SH/duplicate.png ] && cmp -s $HP/r1.txt \"$HP/r1 copy ñ.txt\" && ghas 'r1 copy ñ.txt |'"
  gui $AC WINSCP_DIALOG_ACCEPT=duplicate WINSCP_UITEST="$N;select|right|r1.txt;duplicate|right"
  chk "Duplicate onto itself is an error" "ghas 'same file'"
  gui $AC WINSCP_DIALOG_SET="duplicate.target=$RP/sub" WINSCP_DIALOG_ACCEPT=duplicate WINSCP_UITEST="$N;select|right|r1.txt, r2.log;duplicate|right"
  chk "Duplicate two files into another directory" "cmp -s $HP/r1.txt $HP/sub/r1.txt && cmp -s $HP/r2.log $HP/sub/r2.log"
  gui WINSCP_DIALOG_SET="duplicate.name=a2.txt" WINSCP_DIALOG_ACCEPT=duplicate WINSCP_UITEST="nav|left|$PL;select|left|a.txt;duplicate|left"
  chk "Duplicate a local file" "cmp -s $PL/a.txt $PL/a2.txt"
  echo inner > "$PL/dirA/in.txt"
  gui WINSCP_DIALOG_SET="duplicate.name=dirB" WINSCP_DIALOG_ACCEPT=duplicate WINSCP_UITEST="nav|left|$PL;select|left|dirA;duplicate|left"
  chk "Duplicate a local directory tree" "diff -r $PL/dirA $PL/dirB >/dev/null"
  gui WINSCP_DIALOG_SET="duplicate.name=b.txt" WINSCP_DIALOG_ACCEPT=duplicate WINSCP_UITEST="nav|left|$PL;select|left|a.txt;duplicate|left"
  chk "Duplicate never overwrites an existing local file" "ghas 'already exists' && [ \"\$(cat $PL/b.txt)\" = two22 ]"
  gui WINSCP_DIALOG_SET="duplicate.target=$PL/dirA/x;duplicate.name=dirA" WINSCP_DIALOG_ACCEPT=duplicate WINSCP_UITEST="nav|left|$PL;select|left|dirA;duplicate|left"
  chk "a local directory is not duplicated into itself" "ghas 'does not exist\\|into itself' && [ ! -e $PL/dirA/x ]"

  gui $AC WINSCP_DIALOG_SHOTS=$SH WINSCP_DIALOG_SET="moveto.target=$RP/sub/deep;moveto.name=r2 moved.log" WINSCP_DIALOG_ACCEPT=moveto WINSCP_UITEST="$N;select|right|r2.log;moveto|right;print|right"
  chk "Move To dialog: a remote file into another directory under a new name" "[ -s $SH/moveto.png ] && [ ! -e $HP/r2.log ] && [ \"\$(cat \"$HP/sub/deep/r2 moved.log\")\" = r2 ] && ! ghas '  r2.log |'"
  gui $AC WINSCP_DIALOG_SET="moveto.target=$RP/sub/deep" WINSCP_DIALOG_ACCEPT=moveto WINSCP_UITEST="$N;select|right|sub;moveto|right"
  chk "a remote directory is not moved into itself" "ghas 'into itself' && [ -d $HP/sub/deep ]"
  gui WINSCP_DIALOG_SET="moveto.target=$PL/dirA" WINSCP_DIALOG_ACCEPT=moveto WINSCP_UITEST="nav|left|$PL;select|left|c.md, old.bak;moveto|left;print|left"
  chk "Move To: two local files into a subdirectory" "[ -f $PL/dirA/c.md ] && [ -f $PL/dirA/old.bak ] && [ ! -e $PL/c.md ] && ! ghas '  old.bak |'"

  echo "== panels: reconnect"
  gui $AC WINSCP_UITEST="$N;dropsession;nav|right|$RP/sub;wait|600;print|right;mainstatus;tabs"
  chk "a lost session is offered to be reconnected; the tab and the directory come back" \
      "ghas 'Connection lost' && ghas '^\\[panel right\\] $RP\$' && ghas '  r1.txt\\|  rl ' && ghas \"protocol='SFTP-\" && ghas 'tabs\\] count=1 current=0 engine-active=0'"
  rm -rf /tmp/dlq
  gui $AC WINSCP_UITEST="$N;nav|left|$PL/dirA;dropsession;select|right|rl;xfer|right;waitidle;wait|600;select|right|rl;xfer|right;waitidle;print|left"
  chk "a transfer that fails because the session is gone offers the reconnect; the next transfer works" "ghas 'Connection lost' && [ \"\$(cat $PL/dirA/rl 2>/dev/null)\" = r1 ]"
  gui $AC WINSCP_UITEST="connect|127.0.0.1:2224:jumper:jump456:sftp;tabs;dropsession;nav|right|/config/logs;wait|600;tabs;tab|0;print|right"
  chk "reconnecting the second tab leaves the first one alone" \
      "ghas 'Connection lost' && [ \$(gcount 'tabs\\] count=2 current=1 engine-active=1') = 2 ] && ghas '^\\[panel right\\] /config\$'"
  if docker exec freescp-e2e-jump true >/dev/null 2>&1; then
    ( sleep 4; docker exec freescp-e2e-jump sh -c 'pkill -f "sshd.*jumper"; pkill -f sshd-session; pkill -f sftp-server' >/dev/null 2>&1 ) &
    gui WINSCP_AUTOCONNECT=127.0.0.1:2224:jumper:jump456:sftp WINSCP_UITEST="print|right;wait|7000;nav|right|/config/logs;wait|1500;nav|right|/config/logs;print|right;mainstatus"
    wait
    chk "the server drops the connection for real: noticed at the next action, reconnected" \
        "ghas 'Connection lost' && ghas '^\\[panel right\\] /config/logs\$' && ghas \"protocol='SFTP-\""
  else skip "connection dropped by the server (docker exec not available)"; fi

  echo "== panels: keep remote directory up to date"
  KL="$W/keep"; KR=/config/e2e-keep; HK="$DATA/sshhome/e2e-keep"
  keepsetup() { rm -rf "$KL" "$HK"; mkdir -p "$KL/sub" "$HK"; echo a > "$KL/a.txt"; echo g > "$KL/gone.txt"; echo s > "$KL/sub/s.txt"; cp -R "$KL/." "$HK/"; chmod -R a+rwX "$HK"; }
  KN="nav|left|$KL;nav|right|$KR;keepuptodate"
  keepsetup
  ( sleep 4; echo new > "$KL/new ñ.txt"; sleep 3; echo changed >> "$KL/a.txt"; mkdir -p "$KL/nd/deep"; echo x > "$KL/nd/deep/x.txt"; rm "$KL/gone.txt"; echo s2 >> "$KL/sub/s.txt" ) &
  gui $AC WINSCP_KEEP_MS=17000 WINSCP_DIALOG_SHOTS=$SH WINSCP_DIALOG_SET="keepuptodate.delete=1" WINSCP_UITEST="$KN;print|right"
  wait
  chk "local changes are reflected on the server while watching: new, modified, new tree, deleted (with 'Delete files')" \
      "[ $RC -eq 0 ] && diff -r $KL $HK >/dev/null && ghas 'Upload  new ñ.txt' && ghas 'Delete remote  gone.txt' && ghas 'Upload  sub/s.txt' && ghas 'Stopped' && [ -s $SH/keepuptodate.png ]"
  chk "afterwards the remote panel shows the new state" "ghas '  nd/ |' && ! ghas '  gone.txt |'"
  keepsetup
  ( sleep 4; rm "$KL/gone.txt"; echo new > "$KL/new.txt" ) &
  gui $AC WINSCP_KEEP_MS=11000 WINSCP_DIALOG_SET="keepuptodate.delete=0" WINSCP_UITEST="$KN"
  wait
  chk "without 'Delete files' a file removed locally stays on the server" "[ -f $HK/gone.txt ] && [ -f $HK/new.txt ] && ! ghas 'Delete remote'"
  keepsetup
  ( sleep 4; echo deep >> "$KL/sub/s.txt"; echo top > "$KL/top.txt" ) &
  gui $AC WINSCP_KEEP_MS=11000 WINSCP_DIALOG_SET="keepuptodate.subdirs=0" WINSCP_UITEST="$KN"
  wait
  chk "without 'Update subdirectories' only the directory's own files are watched" "[ -f $HK/top.txt ] && [ \"\$(cat $HK/sub/s.txt)\" = s ]"
  keepsetup; echo pre > "$KL/pre-existing.txt"
  gui $AC WINSCP_KEEP_MS=6000 WINSCP_DIALOG_SET="keepuptodate.initial=1;keepuptodate.subdirs=1" WINSCP_UITEST="$KN"
  chk "'Synchronize when starting' uploads what already differs" "[ \"\$(cat $HK/pre-existing.txt 2>/dev/null)\" = pre ] && ghas 'Upload  pre-existing.txt'"
  keepsetup; echo pre > "$KL/pre-existing.txt"
  gui $AC WINSCP_KEEP_MS=6000 WINSCP_DIALOG_SET="keepuptodate.initial=0" WINSCP_UITEST="$KN"
  chk "without it, what already differs is left alone until it changes" "[ ! -e $HK/pre-existing.txt ]"
  keepsetup
  gui $AC WINSCP_KEEP_MS=4000 WINSCP_DIALOG_SET="keepuptodate.remote=/config/e2e-keep-nope;keepuptodate.delete=1;keepuptodate.initial=1" WINSCP_UITEST="$KN"
  chk "a remote directory that cannot be opened: refused, nothing is watched or uploaded" "ghas 'cannot be opened' && ! ghas 'Watching' && [ ! -e $DATA/sshhome/e2e-keep-nope ]"
  gui $AC WINSCP_KEEP_MS=4000 WINSCP_DIALOG_SET="keepuptodate.local=$W/e2e-keep-nope" WINSCP_UITEST="$KN"
  chk "a local directory that does not exist: refused" "ghas 'does not exist' && ! ghas 'Watching'"
  gui WINSCP_KEEP_MS=3000 WINSCP_UITEST="keepuptodate"
  chk "without a session it says so" "ghas 'Connect a remote session first'"
  KF="$DATA/ftphome/e2e-keep"; rm -rf "$KL" "$KF"; mkdir -p "$KL" "$KF"; chmod 777 "$KF"; echo a > "$KL/a.txt"
  ( sleep 4; echo f1 > "$KL/f1.txt"; mkdir "$KL/fd"; echo f2 > "$KL/fd/f2.txt" ) &
  gui WINSCP_AUTOCONNECT=127.0.0.1:21:winscp:winscp123:ftp WINSCP_KEEP_MS=13000 WINSCP_DIALOG_SET="keepuptodate.initial=1;keepuptodate.delete=0" WINSCP_UITEST="nav|left|$KL;nav|right|/home/winscp/e2e-keep;keepuptodate"
  wait
  chk "the same over FTP" "[ \"\$(cat $KF/a.txt 2>/dev/null)\" = a ] && [ \"\$(cat $KF/f1.txt 2>/dev/null)\" = f1 ] && [ \"\$(cat $KF/fd/f2.txt 2>/dev/null)\" = f2 ]"
  rm -rf "$HK" "$KF" 2>/dev/null

  echo "== panels: server information, icons"
  gui $AC WINSCP_UITEST="sessioninfo"
  chk "Server and Protocol Information: protocol, encryption, capabilities" \
      "ghas '^File transfer protocol: SFTP-' && ghas '^Encryption algorithm' && ghas '^Can create symbolic link: Yes' && ghas '^Can check available space: Yes'"
  for t in 0 1 2; do rm -f "$SH/sessioninfo.png"; gui $AC WINSCP_DIALOG_SHOTS=$SH WINSCP_INFO_TAB=$t WINSCP_UITEST="sessioninfo|dialog"; cp "$SH/sessioninfo.png" "$SH/sessioninfo-$t.png" 2>/dev/null; done
  chk "Server and Protocol Information dialog renders (all three pages)" "[ -s $SH/sessioninfo-0.png ] && [ -s $SH/sessioninfo-1.png ] && [ -s $SH/sessioninfo-2.png ]"
  gui WINSCP_UITEST="sessioninfo|dialog"
  chk "Server and Protocol Information without a session says so" "ghas 'Connect a remote session first'"
  gui WINSCP_UITEST="icons|$SH/icons.png"
  chk "every toolbar icon draws something" "[ -s $SH/icons.png ] && ghas '^\\[icons\\] [4-9][0-9] blank=0\$'"
  gui $AC WINSCP_UITEST="$N;wait|300;shot|$SH/main-panels.png"
  chk "main window renders with a session open" "[ $RC -eq 0 ] && [ -s $SH/main-panels.png ]"
  rm -rf "$HP"
  unset QT_QPA_PLATFORM WINSCP_NO_AUTOLOGIN
  echo "  (screenshots: $SH)"
fi

#==============================================================================================
if want editor; then
  SH="$W/shots"; GO="$W/gui.out"; mkdir -p "$SH"
  export QT_QPA_PLATFORM=offscreen WINSCP_NO_AUTOLOGIN=1
  AC="WINSCP_AUTOCONNECT=127.0.0.1:2223:winscp:winscp123:sftp"
  gui() { tmo 120 env "$@" "$APP" > "$GO" 2>&1; RC=$?; cp "$GO" "$OUT"; }
  ghas() { grep -aq -- "$1" "$GO"; }
  gcount() { grep -ac -- "$1" "$GO"; }
  echo "== editor (offscreen)"
  ED="$W/ed"; rm -rf "$ED"; mkdir -p "$ED"
  printf 'se\xf1or\r\nl\xednea dos\r\ncon\ttab y nbsp\xa0aqu\xed\r\n' > "$ED/latin.txt"
  printf '\xef\xbb\xbfbom line\nsegunda \xc3\xb1\n' > "$ED/bom.txt"
  printf 'plain\nutf8 \xc3\xb1 \xe6\xb5\x8b nbsp\xc2\xa0x\n' > "$ED/utf8.txt"
  printf '\xff\xfeh\x00i\x00\r\x00\n\x00' > "$ED/u16.txt"
  printf 'no final newline' > "$ED/nonl.txt"; : > "$ED/empty.txt"; printf 'banana\n' > "$ED/banana.txt"
  head -c 3000 /dev/urandom > "$ED/bin.dat"; printf 'ab\x00cd' > "$ED/nul.txt"
  for f in latin bom utf8 u16 nonl empty; do cp "$ED/$f.txt" "$ED/$f.orig"; done; cp "$ED/bin.dat" "$ED/bin.orig"
  printf 'added\r\nline' > "$ED/exp-crlf"; printf 'added\nline' > "$ED/exp-lf"; printf 'a\x00d\x00d\x00e\x00d\x00\r\x00\n\x00l\x00i\x00n\x00e\x00' > "$ED/exp-u16"
  edrun() { gui WINSCP_UITEST="nav|left|$ED;select|left|$1;edit|left;editinfo;editappend|added\\nline;editsave;editclose"; }
  edsame() { gui WINSCP_UITEST="nav|left|$ED;select|left|$1;edit|left;editsave;editclose"; }
  for f in latin bom utf8 u16 nonl empty; do edsame $f.txt; cmp -s "$ED/$f.orig" "$ED/$f.txt" || break; done
  chk "editor: open + save changes nothing, whatever the encoding and line endings ($f)" "ghas 'saved=1' && cmp -s $ED/empty.orig $ED/empty.txt && cmp -s $ED/latin.orig $ED/latin.txt && cmp -s $ED/u16.orig $ED/u16.txt && [ $f = empty ]"
  edrun latin.txt
  chk "editor: ISO-8859-1 with CR/LF, a tab and a no-break space: recognised, the rest of the file untouched" \
      "ghas 'encoding=ISO-8859-1 eol=CR/LF lines=4 modified=0' && ghas 'saved=1' && cat $ED/latin.orig $ED/exp-crlf | cmp -s - $ED/latin.txt"
  edrun bom.txt
  chk "editor: UTF-8 with a byte-order mark keeps it" "ghas 'encoding=UTF-8 with BOM eol=LF' && cat $ED/bom.orig $ED/exp-lf | cmp -s - $ED/bom.txt"
  edrun utf8.txt
  chk "editor: UTF-8 (non-ASCII, no-break space)" "ghas 'encoding=UTF-8 eol=LF lines=3' && cat $ED/utf8.orig $ED/exp-lf | cmp -s - $ED/utf8.txt"
  edrun u16.txt
  chk "editor: UTF-16 LE with CR/LF" "ghas 'encoding=UTF-16 LE eol=CR/LF' && cat $ED/u16.orig $ED/exp-u16 | cmp -s - $ED/u16.txt"
  edrun nonl.txt
  chk "editor: a file without a final newline does not get one" "cat $ED/nonl.orig $ED/exp-lf | cmp -s - $ED/nonl.txt"
  gui WINSCP_UITEST="nav|left|$ED;select|left|bin.dat;edit|left;editinfo;select|left|nul.txt;edit|left;editinfo"
  chk "editor: binary files are not opened (saving would damage them)" "[ \$(gcount 'does not look like a text file') = 2 ] && [ \$(gcount '^\\[editor\\] none') = 2 ] && cmp -s $ED/bin.orig $ED/bin.dat"
  gui WINSCP_UITEST="nav|left|$ED;select|left|utf8.txt;edit|left;editfind|UTF8;editfind|UTF8|case;editfind|e2e-absent;editgoto|3;editinfo;editgoto|999;editinfo"
  chk "editor: find (case-insensitive / case-sensitive / absent), go to line" \
      "ghas 'found=1 .* line=2 column=5' && [ \$(gcount 'found=0') = 2 ] && ghas '^\\[editor\\] encoding.* line=3 column=1' && ghas ' line=4 column=1'"
  gui WINSCP_UITEST="nav|left|$ED;select|left|banana.txt;edit|left;editreplace|a|aa;editreplace|B|X|case;editreplace|b|X;editsave;editclose"
  chk "editor: replace all (also when the replacement contains what is searched)" "ghas 'replaced=3' && ghas 'replaced=0' && ghas 'replaced=1' && [ \"\$(cat $ED/banana.txt)\" = Xaanaanaa ]"
  cp "$ED/latin.orig" "$ED/latin.txt"
  gui WINSCP_UITEST="nav|left|$ED;select|left|latin.txt;edit|left;editappend|测;editsave;editinfo;editclose"
  chk "editor: text an ISO-8859-1 file cannot hold: asks, then saves as UTF-8 (nothing is replaced by '?')" \
      "ghas 'Save it as UTF-8 instead' && ghas 'encoding=UTF-8 eol=CR/LF' && grep -q \"\$(printf 'se\303\261or')\" $ED/latin.txt && grep -q '测' $ED/latin.txt"
  cp "$ED/utf8.orig" "$ED/utf8.txt"
  gui WINSCP_UITEST="nav|left|$ED;select|left|utf8.txt;edit|left;editappend|unsaved;editinfo;editreload;editinfo;editclose"
  chk "editor: reload discards the changes after asking" "ghas 'modified=1' && ghas 'Discard the changes' && ghas 'reloaded=1' && ghas 'lines=3 modified=0' && cmp -s $ED/utf8.orig $ED/utf8.txt"
  gui WINSCP_UITEST="nav|left|$ED;select|left|utf8.txt;edit|left;editappend|kept;editclose"
  chk "editor: closing a modified file asks and saves" "ghas 'was modified. Save the changes' && cat $ED/utf8.orig | tr -d '\\000' | cat - <(printf kept) | cmp -s - $ED/utf8.txt"
  HE="$DATA/sshhome/e2e-ed"; rm -rf "$HE"; mkdir -p "$HE/a" "$HE/b"; chmod -R 777 "$HE"
  printf 'remote\r\nfile \xf1\r\n' > "$HE/r.txt"; cp "$HE/r.txt" "$ED/r.orig"; echo in-a > "$HE/a/same.txt"; echo in-b > "$HE/b/same.txt"; chmod 666 "$HE"/r.txt "$HE"/*/same.txt
  gui $AC WINSCP_UITEST="nav|right|/config/e2e-ed;select|right|r.txt;edit|right;editinfo;editappend|added\\nline;editsave;wait|2500;editclose"
  chk "editor: a remote file is downloaded, edited and uploaded on save, in its own encoding and line endings" \
      "ghas 'encoding=ISO-8859-1 eol=CR/LF' && ghas 'saved=1' && cat $ED/r.orig $ED/exp-crlf | cmp -s - $HE/r.txt"
  gui $AC WINSCP_UITEST="nav|right|/config/e2e-ed/a;select|right|same.txt;edit|right;nav|right|/config/e2e-ed/b;select|right|same.txt;edit|right;editappend|B;editsave;wait|2500;editclose;editappend|A;editsave;wait|2500;editclose"
  chk "editor: two remote files of the same name from different directories are kept apart" "[ \"\$(tr -d '\\n' < $HE/a/same.txt)\" = in-aA ] && [ \"\$(tr -d '\\n' < $HE/b/same.txt)\" = in-bB ]"
  printf 'changed on the server\n' > "$ED/srv.txt"
  gui $AC WINSCP_UITEST="nav|right|/config/e2e-ed;select|right|r.txt;edit|right;nav|right|/config;editreload;editinfo;editclose"
  chk "editor: reload of a remote file reads it from its own directory, wherever the panel is now" "ghas 'reloaded=1' && ghas 'lines=4 modified=0'"
  gui WINSCP_UITEST="nav|left|$ED;select|left|utf8.txt;edit|left;editshot|$SH/editor.png;editclose"
  chk "editor window renders (with the find / replace bar)" "[ -s $SH/editor.png ]"
  rm -rf "$HE"
  unset QT_QPA_PLATFORM WINSCP_NO_AUTOLOGIN
fi

echo
echo "passed $PASSED, failed $FAILED, skipped $SKIPPED"
[ $FAILED -eq 0 ] && echo "ALL OK" || echo "SOME CHECKS FAILED"
exit $([ $FAILED -eq 0 ] && echo 0 || echo 1)
