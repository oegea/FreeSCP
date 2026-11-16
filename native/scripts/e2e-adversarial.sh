#!/usr/bin/env bash
# Adversarial end-to-end tests: the situations that broke (or could break) the port, kept as
# regression checks. Complements e2e-features.sh; same servers (native/scripts/e2e/servers.sh).
#
#   - time zones (5 zones, winter + summer files): timestamps survive upload and download
#   - internal ("plain") transfers ignore the user's Transfer Settings
#   - cancelling a transfer leaves a usable session (all protocols; FTP's "226 already sent" race)
#   - a server that dies or freezes mid-operation: an error within the timeout, never a hang
#   - FTP timers: long transfers are not timed out, an idle session is kept alive
#   - 500 files in one session with a 256-descriptor limit: no descriptor leak
#   - Synchronize "delete local" never follows a symbolic link
#   - file names containing ? * \\ % and spaces
#   - GUI: several sessions in tabs, sites saved by 0.2, a Copy target that does not exist,
#     quitting while a transfer runs
#
# Usage: native/scripts/e2e-adversarial.sh <build-dir> <data-dir>      (takes about 12 minutes)
# It restarts / pauses the test containers while it runs.
set -uo pipefail
BUILD="$(cd "${1:?build dir}" && pwd)"; D="$(cd "${2:?data dir (servers.sh up)}" && pwd)"
. "$D/env.sh"
C="$BUILD/ui-qt/freescp-bridge-cli"
if [ -x "$BUILD/ui-qt/winscp-qt.app/Contents/MacOS/winscp-qt" ]; then APP="$BUILD/ui-qt/winscp-qt.app/Contents/MacOS/winscp-qt"
else APP="$BUILD/ui-qt/winscp-qt"; fi
W=/tmp/freescp-adv; rm -rf "$W"; mkdir -p "$W/dl" "$W/l"; OUT="$W/out"; GO="$W/gui.out"
export WINSCP_INI="$W/engine.ini"
FAILED=0; PASSED=0
ok()  { echo "  ok    $1"; PASSED=$((PASSED+1)); }
bad() { echo "  FAIL  $1"; FAILED=$((FAILED+1)); [ -f "$OUT" ] && tail -6 "$OUT" | sed 's/^/        | /'; }
chk() { if eval "$2"; then ok "$1"; else bad "$1"; fi; }
tmo() { local s="$1"; shift; perl -e 'alarm shift; exec @ARGV or die "exec: $!"' "$s" "$@"; }
run() { tmo ${TMO:-240} "$C" "$@" > "$OUT" 2>&1; RC=$?; }
has() { grep -aq -- "$1" "$OUT"; }
mt()  { if stat -f %m "$1" >/dev/null 2>&1; then stat -f %m "$1"; else stat -c %Y "$1"; fi; }
SSH="port=2223 user=winscp pass=winscp123"; FTP="port=21 user=winscp pass=winscp123 proto=ftp"
DAV="port=8087 user=winscp pass=winscp123 proto=dav"; S3="port=9101 user=minioadmin pass=minioadmin123 proto=s3"

echo "== time zones: timestamps survive, and the listing shows local wall time"
echo w > $W/w.txt; echo s > $W/s.txt
for TZN in America/New_York Asia/Kolkata Australia/Sydney UTC Europe/Madrid; do
  export TZ=$TZN
  touch -t 202601151234.56 $W/w.txt; touch -t 202607151234.56 $W/s.txt
  for p in "sftp|$SSH|/config/adv|$D/sshhome/adv" "scp|$SSH proto=scp|/config/adv|$D/sshhome/adv" "ftp|$FTP|/home/winscp/adv|$D/ftphome/adv"; do
    IFS='|' read -r name opts R H <<< "$p"
    rm -rf $H $W/dl; mkdir -p $H $W/dl; chmod 777 $H
    run $opts -- "put|$W/w.txt|$R" "put|$W/s.txt|$R" "ls|$R" "get|$R/w.txt|$W/dl" "get|$R/s.txt|$W/dl"
    LW=$(grep -a ' w.txt' $OUT | sed -E 's/.*\[(.*)\].*/\1/'); LS=$(grep -a ' s.txt' $OUT | sed -E 's/.*\[(.*)\].*/\1/')
    chk "$TZN $name: the server keeps both modification times, the download keeps the winter file's" \
      "[ $RC -eq 0 ] && [ \$(mt $H/w.txt) = \$(mt $W/w.txt) ] && [ \$(mt $H/s.txt) = \$(mt $W/s.txt) ] && [ \$(mt $W/dl/w.txt) = \$(mt $W/w.txt) ]"
    # Known limit inherited from WinSCP: an FTP server without MLSD (vsftpd) lists local times with
    # no zone; ONE offset is detected and applied to every file, so a file dated on the other side
    # of a DST change comes back one hour off (and to the minute). SFTP and SCP are exact.
    if [ $name != ftp ]; then chk "$TZN $name: ...and the summer file's" "[ \$(mt $W/dl/s.txt) = \$(mt $W/s.txt) ]"; fi
    # SFTP carries UTC, so the listing is local wall time in any zone and season. SCP lists the
    # server's own clock, and FTP without MLSD applies ONE detected offset to all files (a file from
    # the other side of a DST change shows an hour off) - both as in WinSCP.
    if [ $name = sftp ]; then chk "$TZN sftp: the listing shows local wall time ('$LW' / '$LS')" "echo '$LW' | grep -q 'Jan 15 12:34:56' && echo '$LS' | grep -q 'Jul 15 12:34:56'"; fi
  done
done
unset TZ

echo "== plain (internal) transfers ignore the user's transfer settings"
H=$D/sshhome/adv; R=/config/adv; rm -rf $H $W/dl; mkdir -p $H $W/dl
printf 'x\ny\n' > $W/Note.TXT
run $SSH crlf=1 -- "set|mask=*.jpg|case=upper|mode=text|speed=1|resume=on" "putplain|$W/Note.TXT|$R" "getplain|$R/Note.TXT|$W/dl"
chk "putplain/getplain with mask=*.jpg, UPPER, text mode, 1 KB/s limit: same name, same bytes, instantly" "[ $RC -eq 0 ] && cmp -s $W/Note.TXT $H/Note.TXT && cmp -s $W/Note.TXT $W/dl/Note.TXT"
run $SSH -- "set|mask=*.jpg" "put|$W/Note.TXT|$R"

echo "== cancel mid-transfer leaves a usable session"
head -c 6000000 /dev/urandom > $W/c.bin; rm -rf $H; mkdir -p $H
for p in "sftp|$SSH|/config/adv|$D/sshhome/adv" "scp|$SSH proto=scp|/config/adv|$D/sshhome/adv" "ftp|$FTP|/home/winscp/adv|$D/ftphome/adv" "dav|$DAV|/adv|$D/davdata/data/adv" "s3|$S3|/testbucket/adv|$D/s3data/testbucket/adv"; do
  IFS='|' read -r name opts R H <<< "$p"; rm -rf $H $W/dl; mkdir -p $H $W/dl; chmod 777 $H
  run $opts -- "set|resume=off" "cancelafter|1000000" "put|$W/c.bin|$R" "cancelafter|0" "ls|$R" "put|$W/c.bin|$R" "get|$R/c.bin|$W/dl"
  # (S3: the "directory" does not exist until something is stored in it, so the ls may fail there)
  chk "$name: cancelled upload fails, then ls + full upload + download work on the same session" "grep -aq 'FAIL put' $OUT && { [ $name = s3 ] || grep -aq 'OK   ls' $OUT; } && [ \$(grep -ac 'OK   put' $OUT) = 1 ] && cmp -s $W/c.bin $W/dl/c.bin"
  rm -rf $W/dl; mkdir -p $W/dl
  run $opts -- "set|resume=off" "cancelafter|1000000" "get|$R/c.bin|$W/dl" "cancelafter|0" "get|$R/c.bin|$W/dl"
  chk "$name: cancelled download fails, retry is complete" "grep -aq 'FAIL get' $OUT && cmp -s $W/c.bin $W/dl/c.bin"
done

echo "== server dies mid-transfer: a reported error, no hang, no crash"
H=$D/sshhome/adv; R=/config/adv
( sleep 4; docker restart -t 0 freescp-e2e-sshd >/dev/null 2>&1 ) &
T0=$(date +%s); TMO=60 run $SSH -- "set|speed=200" "put|$W/c.bin|$R"; T1=$(date +%s); wait
chk "sftp upload: rc=$RC after $((T1-T0)) s: $(grep -a 'FAIL' $OUT | head -1 | cut -c1-90)" "[ $RC -eq 2 ] && [ $((T1-T0)) -lt 45 ]"
sleep 6
( sleep 4; docker restart -t 0 freescp-e2e-ftp >/dev/null 2>&1 ) &
T0=$(date +%s); TMO=90 run $FTP -- "set|speed=200" "put|$W/c.bin|/home/winscp/adv"; T1=$(date +%s); wait
chk "ftp upload: rc=$RC after $((T1-T0)) s: $(grep -a 'FAIL' $OUT | head -1 | cut -c1-90)" "[ $RC -eq 2 ] && [ $((T1-T0)) -lt 80 ]"
sleep 6
( sleep 5; docker restart -t 0 freescp-e2e-jump >/dev/null 2>&1 ) &
T0=$(date +%s); TMO=90 run host=freescp-e2e-sshd port=2222 user=winscp pass=winscp123 tunnel=127.0.0.1:2224:jumper:jump456 -- "set|speed=200" "put|$W/c.bin|$R"; T1=$(date +%s); wait
chk "upload through a tunnel whose jump host dies: rc=$RC after $((T1-T0)) s: $(grep -a 'FAIL' $OUT | head -1 | cut -c1-90)" "[ $RC -ne 0 ] && [ $RC -ne 142 ] && [ $RC -lt 128 ]"
sleep 8

echo "== text / automatic mode on FTP, WebDAV, S3 (Unix servers): content must come back identical"
printf 'l1\nl2\n' > $W/t.txt; head -c 5000 /dev/urandom > $W/b.bin; printf '\r\n\n\r' >> $W/b.bin
for p in "ftp|$FTP|/home/winscp/adv|$D/ftphome/adv" "dav|$DAV|/adv|$D/davdata/data/adv" "s3|$S3|/testbucket/adv|$D/s3data/testbucket/adv"; do
  IFS='|' read -r name opts R H <<< "$p"; rm -rf $H $W/dl; mkdir -p $H $W/dl; chmod 777 $H
  run $opts -- "set|mode=text" "put|$W/t.txt|$R" "get|$R/t.txt|$W/dl"
  chk "$name text mode: LF file round trip identical" "[ $RC -eq 0 ] && cmp -s $W/t.txt $W/dl/t.txt"
  rm -rf $W/dl; mkdir -p $W/dl
  run $opts -- "set|mode=auto" "put|$W/b.bin|$R" "put|$W/t.txt|$R" "get|$R/b.bin|$W/dl" "get|$R/t.txt|$W/dl"
  chk "$name automatic mode: binary file with CR/LF bytes untouched, text file identical" "[ $RC -eq 0 ] && cmp -s $W/b.bin $W/dl/b.bin && cmp -s $W/t.txt $W/dl/t.txt"
done

echo "== edge sizes"
: > $W/empty.dat; head -c 32768 /dev/urandom > $W/blk.dat; head -c 262144 /dev/urandom > $W/blk2.dat; head -c 1 /dev/urandom > $W/one.dat
for p in "sftp|$SSH|/config/adv|$D/sshhome/adv" "scp|$SSH proto=scp|/config/adv|$D/sshhome/adv" "ftp|$FTP|/home/winscp/adv|$D/ftphome/adv" "dav|$DAV|/adv|$D/davdata/data/adv" "s3|$S3|/testbucket/adv|$D/s3data/testbucket/adv"; do
  IFS='|' read -r name opts R H <<< "$p"; rm -rf $H $W/dl; mkdir -p $H $W/dl; chmod 777 $H
  run $opts -- "set|resume=on" "put|$W/empty.dat|$R" "put|$W/one.dat|$R" "put|$W/blk.dat|$R" "put|$W/blk2.dat|$R" "get|$R/empty.dat|$W/dl" "get|$R/one.dat|$W/dl" "get|$R/blk.dat|$W/dl" "get|$R/blk2.dat|$W/dl"
  chk "$name: 0 B, 1 B, 32 KiB, 256 KiB (resume=on) round trip" "[ $RC -eq 0 ] && cmp -s $W/empty.dat $W/dl/empty.dat && cmp -s $W/one.dat $W/dl/one.dat && cmp -s $W/blk.dat $W/dl/blk.dat && cmp -s $W/blk2.dat $W/dl/blk2.dat"
done
echo "== download into a directory that cannot be written"
mkdir -p $W/ro; chmod 555 $W/ro
run $SSH -- "get|/config/adv/blk.dat|$W/ro"; chmod 755 $W/ro
chk "readable error, rc=$RC: $(grep -a FAIL $OUT | cut -c1-100)" "[ $RC -eq 2 ]"

R=/home/winscp/adv2; H=$D/ftphome/adv2
rm -rf $H; mkdir -p $H; chmod 777 $H
head -c 6000000 /dev/urandom > $W/c.bin; head -c 30000000 /dev/urandom > $W/long.bin

echo "== FTP: cancel a download the server has already finished sending, then keep using the session"
for i in 1 2 3; do
  rm -rf $W/dl; mkdir -p $W/dl
  TMO=60 run $FTP -- "put|$W/c.bin|$R" "set|resume=off" "cancelafter|1000000" "get|$R/c.bin|$W/dl" "cancelafter|0" "ls|$R" "get|$R/c.bin|$W/dl"
  chk "round $i: cancelled, then ls + complete download (rc=$RC)" "grep -aq 'FAIL get' $OUT && grep -aq 'OK   ls' $OUT && cmp -s $W/c.bin $W/dl/c.bin"
done
echo "== FTP: long transfers are not mistaken for a dead connection (30 MB at 1 MB/s = 30 s, timeout is 15 s)"
rm -rf $W/dl; mkdir -p $W/dl
T0=$(date +%s); TMO=150 run $FTP -- "set|speed=1000" "put|$W/long.bin|$R" "get|$R/long.bin|$W/dl"; T1=$(date +%s)
chk "upload + download took $((T1-T0)) s, byte-identical" "[ $RC -eq 0 ] && cmp -s $W/long.bin $W/dl/long.bin && [ $((T1-T0)) -ge 50 ]"
rm -rf $W/dl; mkdir -p $W/dl
T0=$(date +%s); TMO=150 run port=2121 user=winscp pass=winscp123 proto=ftp tls=1 -- "set|speed=1000" "put|$W/long.bin|/home/winscp" "get|/home/winscp/long.bin|$W/dl"; T1=$(date +%s)
chk "same over FTPS: $((T1-T0)) s, byte-identical" "[ $RC -eq 0 ] && cmp -s $W/long.bin $W/dl/long.bin"
echo "== FTP: an idle session is kept alive and still works after 75 s"
rm -f /tmp/winscp-session.log
WINSCP_SESSIONLOG=1 TMO=150 run $FTP -- "ls|$R" "sleep|75" "ls|$R" "put|$W/c.bin|$R"
chk "ls after idling, then upload (keep-alive commands sent: $(grep -ac '^> .* \(NOOP\|PWD\|REST 0\|TYPE [AI]\)$' /tmp/winscp-session.log))" "[ $RC -eq 0 ]"
grep -a "^> " /tmp/winscp-session.log | sed -n '/LIST/,$p' | awk '{print $4,$5}' | tr '\n' ';' | cut -c1-300; echo
echo "== FTP: a server that stops answering -> timeout error, not an endless wait"
( sleep 3; docker pause freescp-e2e-ftp >/dev/null 2>&1 ) &
T0=$(date +%s); TMO=120 run $FTP -- "ls|$R" "sleep|6" "ls|$R"; T1=$(date +%s); wait; docker unpause freescp-e2e-ftp >/dev/null 2>&1
chk "frozen server during a listing: rc=$RC after $((T1-T0)) s: $(grep -a FAIL $OUT | head -1 | cut -c1-110)" "[ $RC -eq 2 ] && [ $((T1-T0)) -lt 60 ]"
sleep 3
( sleep 4; docker pause freescp-e2e-ftp >/dev/null 2>&1 ) &
T0=$(date +%s); TMO=120 run $FTP -- "set|speed=300" "put|$W/c.bin|$R"; T1=$(date +%s); wait; docker unpause freescp-e2e-ftp >/dev/null 2>&1
chk "frozen server during an upload: rc=$RC after $((T1-T0)) s: $(grep -a FAIL $OUT | head -1 | cut -c1-110)" "[ $RC -eq 2 ] && [ $((T1-T0)) -lt 80 ]"
sleep 3
echo "== SFTP: a server that stops answering -> timeout error"
( sleep 3; docker pause freescp-e2e-sshd >/dev/null 2>&1 ) &
T0=$(date +%s); TMO=120 run port=2223 user=winscp pass=winscp123 -- "ls" "sleep|6" "ls|/config"; T1=$(date +%s); wait; docker unpause freescp-e2e-sshd >/dev/null 2>&1
chk "frozen SSH server: rc=$RC after $((T1-T0)) s: $(grep -a 'FAIL' $OUT | head -1 | cut -c1-110)" "[ $RC -eq 2 ] && [ $((T1-T0)) -lt 60 ]"

mkdir -p $W/many/sub; for i in $(seq 1 400); do echo "file $i" > $W/many/f$i.txt; done; for i in $(seq 1 100); do echo "s $i" > $W/many/sub/s$i.txt; done

echo "== 500 files up and down in ONE session: no descriptor leak, identical tree (the default limit is 256 descriptors)"
for p in "sftp|$SSH|/config|$D/sshhome" "scp|$SSH proto=scp|/config|$D/sshhome" "ftp|$FTP|/home/winscp|$D/ftphome" "dav|$DAV||$D/davdata/data" "s3|$S3|/testbucket|$D/s3data/testbucket"; do
  IFS='|' read -r name opts base host <<< "$p"; rm -rf $host/many $W/dl; mkdir -p $W/dl; sleep 3   # (a host-side delete reaches the container with a delay)
  ( ulimit -n 256; run $opts -- fds "put|$W/many|${base:-/}" fds "get|$base/many|$W/dl" fds; echo $RC > $W/rc )
  RC=$(cat $W/rc); F=$(grep -a '^FDS' $OUT | awk '{print $2}' | tr '\n' ' ')
  set -- $F
  chk "$name: rc=$RC, descriptors before/after upload/after download: $F" "[ $RC -eq 0 ] && diff -r $W/many $W/dl/many >/dev/null && [ \$(( ${3:-999} - ${1:-0} )) -le 3 ]"
done

echo "== Synchronize 'delete local' never follows a symbolic link"
L=$W/sy; H=$D/sshhome/adv3-sy; rm -rf $L $H $W/precious; mkdir -p $L $H $W/precious/deep
echo keep > $W/precious/important.txt; echo keep2 > $W/precious/deep/more.txt
echo a > $L/a.txt; cp -p $L/a.txt $H/a.txt
ln -s $W/precious $L/link-to-dir; ln -s $W/precious/important.txt $L/link-to-file; mkdir -p $L/realdir; ln -s $W/precious $L/realdir/inner-link
sleep 2
run $SSH -- "sync|$L|/config/adv3-sy|down|delete"
cat $OUT | grep -a "Delete\|SYNC\|FAIL" | sed 's/^/        /'
chk "the linked directory's contents are untouched" "[ -f $W/precious/important.txt ] && [ -f $W/precious/deep/more.txt ]"
chk "the links themselves (top level and inside a deleted directory) are gone, a.txt stays" "[ ! -L $L/link-to-dir ] && [ ! -L $L/link-to-file ] && [ ! -e $L/realdir ] && [ -f $L/a.txt ]"

echo "== local file names with characters that look like masks or separators"
O=$W/odd; mkdir -p $O; H=$D/sshhome/adv3-odd; rm -rf $H; mkdir -p $H
echo q > "$O/what?.txt"; echo w > "$O/whatX.txt"; echo s > "$O/star*.txt"; echo b > "$O/back\\slash.txt"; echo sp > "$O/ lead and trail .txt"; echo h > "$O/#hash&amp;.txt"
rm -rf $W/dl; mkdir -p $W/dl
run $SSH -- "put|$O/what?.txt|/config/adv3-odd" "put|$O/star*.txt|/config/adv3-odd" "put|$O/back\\slash.txt|/config/adv3-odd" "put|$O/ lead and trail .txt|/config/adv3-odd" "put|$O/#hash&amp;.txt|/config/adv3-odd" "get|/config/adv3-odd/what?.txt|$W/dl" "get|/config/adv3-odd/star*.txt|$W/dl" "get|/config/adv3-odd/#hash&amp;.txt|$W/dl"
chk "uploaded under their exact names with their own content (rc=$RC)" "[ $RC -eq 0 ] && [ \"\$(cat \"$H/what?.txt\")\" = q ] && [ \"\$(cat \"$H/star*.txt\")\" = s ] && [ -f \"$H/ lead and trail .txt\" ] && [ ! -e $H/whatX.txt ]"
chk "downloaded under their exact names too" "[ \"\$(cat \"$W/dl/what?.txt\")\" = q ] && [ \"\$(cat \"$W/dl/star*.txt\")\" = s ]"
echo pct > "$O/100%25 done.txt"; run $SSH -- "put|$O/100%25 done.txt|/config/adv3-odd"
chk "a literal %XX in a local name is not decoded on upload" "[ -f \"$H/100%25 done.txt\" ]"
ls "$H" | sed 's/^/        [/; s/$/]/'; ls $W/dl | sed 's/^/        dl: [/; s/$/]/'

#----------------------------------------------------------------------------------------------
# GUI (offscreen)
export QT_QPA_PLATFORM=offscreen WINSCP_NO_AUTOLOGIN=1
gui() { tmo 150 env "$@" "$APP" > "$GO" 2>&1; RC=$?; cp "$GO" "$OUT"; }
ghas() { grep -aq -- "$1" "$GO"; }
AC="WINSCP_AUTOCONNECT=127.0.0.1:2223:winscp:winscp123:sftp"
defaults delete com.winscp-native-port-test.WinSCP >/dev/null 2>&1
rm -rf $D/sshhome/adv4 $D/ftphome/adv4 $D/davdata/data/adv4; mkdir -p $D/sshhome/adv4 $D/ftphome/adv4 $D/davdata/data/adv4; chmod 777 $D/ftphome/adv4 $D/davdata/data/adv4
echo ssh > $D/sshhome/adv4/on-ssh.txt; echo ftp > $D/ftphome/adv4/on-ftp.txt; echo dav > $D/davdata/data/adv4/on-dav.txt
echo up > $W/l/local.txt; rm -f $D/sshhome/adv4/local.txt $D/ftphome/adv4/local.txt; sleep 3

echo "== three sessions in tabs (SFTP, FTP, WebDAV): each tab keeps its own server and directory"
gui $AC WINSCP_UITEST="nav|right|/config/adv4;connect|127.0.0.1:21:winscp:winscp123:ftp;nav|right|/home/winscp/adv4;connect|127.0.0.1:8087:winscp:winscp123:dav;nav|right|/adv4;tabs;print|right;tab|0;tabs;print|right;tab|1;tabs;print|right;tab|2;print|right"
chk "tabs: 3 open, switching shows the right listing for each (rc=$RC)" "[ $RC -eq 0 ] && ghas 'count=3' && [ \"\$(grep -a 'on-' $GO | awk '{print \$1}' | tr '\n' ',')\" = 'on-dav.txt,on-ssh.txt,on-ftp.txt,on-dav.txt,' ]"
gui $AC WINSCP_UITEST="nav|right|/config/adv4;connect|127.0.0.1:21:winscp:winscp123:ftp;nav|right|/home/winscp/adv4;tab|0;nav|left|$W/l;select|left|local.txt;xfer|left;waitidle;tab|1;nav|left|$W/l;select|left|local.txt;xfer|left;waitidle;tab|0;print|right;tab|1;print|right"
chk "an upload goes to the server of the ACTIVE tab (both servers got their copy)" "[ -f $D/sshhome/adv4/local.txt ] && [ -f $D/ftphome/adv4/local.txt ]"
gui $AC WINSCP_UITEST="nav|right|/config/adv4;connect|127.0.0.1:21:winscp:winscp123:ftp;nav|right|/home/winscp/adv4;closetab|0;tabs;nav|right|/home/winscp/adv4;print|right;closetab|0;tabs;print|right"
chk "closing tabs: the remaining session keeps working, the last close returns to a local panel" "ghas 'count=1' && ghas 'on-ftp.txt' && ghas 'count=0'"

echo "== sites saved by 0.2 (no tls / tunnel / proxy keys) still load and connect"
defaults write com.winscp-native-port-test.WinSCP "sites.old-ssh.protocol" -int 0
defaults write com.winscp-native-port-test.WinSCP "sites.old-ssh.host" -string 127.0.0.1
defaults write com.winscp-native-port-test.WinSCP "sites.old-ssh.port" -int 2223
defaults write com.winscp-native-port-test.WinSCP "sites.old-ssh.user" -string winscp
defaults write com.winscp-native-port-test.WinSCP "sites.old-ssh.pass" -string winscp123
defaults write com.winscp-native-port-test.WinSCP "sites.old-ssh.keyFile" -string ""
defaults write com.winscp-native-port-test.WinSCP "sites.old-ftp.protocol" -int 2
defaults write com.winscp-native-port-test.WinSCP "sites.old-ftp.host" -string 127.0.0.1
defaults write com.winscp-native-port-test.WinSCP "sites.old-ftp.port" -int 21
defaults write com.winscp-native-port-test.WinSCP "sites.old-ftp.user" -string winscp
defaults write com.winscp-native-port-test.WinSCP "sites.old-ftp.pass" -string winscp123
sleep 1
gui WINSCP_AUTOCONNECT_SITE=old-ssh WINSCP_UITEST="nav|right|/config/adv4;print|right"; chk "0.2 SFTP site" "ghas 'on-ssh.txt'"
gui WINSCP_AUTOCONNECT_SITE=old-ftp WINSCP_UITEST="nav|right|/home/winscp/adv4;print|right"; chk "0.2 FTP site (plain, passive)" "ghas 'on-ftp.txt'"
gui WINSCP_DIALOG_SHOTS=$W WINSCP_LOGIN_SELECT_SITE=old-ftp WINSCP_UITEST="login"; chk "the Login dialog shows it without errors" "[ $RC -eq 0 ] && [ -s $W/login.png ]"

echo "== Copy dialog with a target directory that does not exist: nothing is transferred elsewhere"
rm -f $D/sshhome/adv4/local.txt; sleep 2
gui $AC WINSCP_DIALOG_ACCEPT=copy WINSCP_COPY_TARGET=/config/adv4/no-such-dir WINSCP_UITEST="nav|left|$W/l;nav|right|/config/adv4;select|left|local.txt;copy|left;waitidle;print|right"
chk "upload to a missing directory is cancelled with an error (not dropped into the previous directory)" "ghas 'Error changing directory' && [ ! -f $D/sshhome/adv4/local.txt ]"
gui $AC WINSCP_DIALOG_ACCEPT=copy WINSCP_UITEST="nav|left|$W/l;nav|right|/config/adv4;select|left|local.txt;copy|left;waitidle;print|right"
chk "the same dialog accepted with its default target uploads the file" "[ -f $D/sshhome/adv4/local.txt ]"

echo "== quitting while a background job runs does not crash"
head -c 30000000 /dev/urandom > $W/l/big.bin
gui $AC WINSCP_UITEST="setxfer|defaults=1|speed=500;nav|left|$W/l;nav|right|/config/adv4;select|left|big.bin;xfer|left;wait|1500;quit"
chk "exit code $RC (no crash report)" "[ $RC -eq 0 ] && ! grep -aq 'CRASH' $GO"
gui WINSCP_UITEST="setxfer|defaults=1"
unset QT_QPA_PLATFORM WINSCP_NO_AUTOLOGIN
echo
echo "passed $PASSED, failed $FAILED"
[ $FAILED -eq 0 ] && echo "ALL OK" || echo "SOME CHECKS FAILED"
exit $([ $FAILED -eq 0 ] && echo 0 || echo 1)
