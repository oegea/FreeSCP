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
#
# Usage:  native/scripts/e2e/servers.sh up <data-dir>
#         native/scripts/e2e-features.sh <build-dir> <data-dir> [section ...]     (default: all)
#
# Uses the bridge CLI (ui-qt/tests/bridgecli.cpp) - the same API the GUI calls - and the app's
# scripted-UI hooks (WINSCP_UITEST, see main.cpp). Everything is created under /tmp/freescp-e2e and
# in dedicated "e2e-*" directories on the servers.
set -uo pipefail

BUILD="$(cd "${1:?build dir}" && pwd)"; DATA="$(cd "${2:?data dir (servers.sh up)}" && pwd)"; shift 2
SECTIONS="${*:-conn auth tunnel proxy ftps xfer resume find sync gui}"
. "$DATA/env.sh"
C="$BUILD/ui-qt/freescp-bridge-cli"
if [ -x "$BUILD/ui-qt/winscp-qt.app/Contents/MacOS/winscp-qt" ]; then APP="$BUILD/ui-qt/winscp-qt.app/Contents/MacOS/winscp-qt"
else APP="$BUILD/ui-qt/winscp-qt"; fi
W=/tmp/freescp-e2e; rm -rf "$W"; mkdir -p "$W/dl" "$W/shots"
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
  done
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

echo
echo "passed $PASSED, failed $FAILED, skipped $SKIPPED"
[ $FAILED -eq 0 ] && echo "ALL OK" || echo "SOME CHECKS FAILED"
exit $([ $FAILED -eq 0 ] && echo 0 || echo 1)
