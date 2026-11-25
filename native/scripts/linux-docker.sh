#!/usr/bin/env bash
# Build and test the Linux port inside Docker, from a Mac (or any Docker host).
#
#   native/scripts/linux-docker.sh <arm64|amd64> build            compile + unit tests
#   native/scripts/linux-docker.sh <arm64|amd64> ssh              e2e-ssh.sh against an sshd inside the container
#   native/scripts/linux-docker.sh <arm64|amd64> features <data-dir> [section ...]
#                                                                 e2e-features.sh against the servers started by
#                                                                 `e2e/servers.sh up <data-dir>` on this machine
#   native/scripts/linux-docker.sh <arm64|amd64> shell            a shell in the container
#
# The source tree is COPIED into a Docker volume (freescp-src-<arch>), not bind-mounted: the volume
# is a case-sensitive Linux file system (a macOS bind mount would hide #include case mistakes) and
# the build tree stays inside it, so a second run is incremental. amd64 on Apple Silicon runs under
# emulation: the first build takes a long time.
#
# `features`: the test servers publish their ports on the Docker host. Inside the container each of
# those ports is forwarded from 127.0.0.1 (socat), and the proxy / keyboard-interactive helper
# servers run in the container itself, so the suite sees the same addresses as on the host.
set -euo pipefail
ARCH="${1:?arm64|amd64}"; CMD="${2:?build|ssh|features|shell}"; shift 2
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../.." && pwd)"
IMG="freescp-linux-test:$ARCH"; VOL="freescp-src-$ARCH"

docker image inspect "$IMG" >/dev/null 2>&1 || \
  docker build --platform "linux/$ARCH" -t "$IMG" -f "$HERE/e2e/Dockerfile.linux" "$HERE/e2e"
docker volume create "$VOL" >/dev/null

# copy the tracked + new (not ignored) files into the volume; keeps the build directory
( cd "$ROOT" && git ls-files -co --exclude-standard -z -- native source libs/neon libs/expat libs/libs3 libs/openssl/include \
    libs/mfc docs/porting ':!native/build*' | tar --null -T - -cf - 2>/dev/null ) | \
  docker run --rm -i --platform "linux/$ARCH" -v "$VOL:/src" "$IMG" bash -c 'chown dev:dev /src && su dev -c "tar -xf - -C /src --no-same-owner 2>/dev/null"; chown -R dev:dev /src/native/build-linux 2>/dev/null; true'

RUN=(docker run --rm --platform "linux/$ARCH" -v "$VOL:/src" -w /src --add-host host.docker.internal:host-gateway)
[ -t 0 ] && RUN+=(-it)

case "$CMD" in
  build)
    "${RUN[@]}" "$IMG" su dev -c 'native/scripts/build-linux.sh Release 2>&1 | tail -25 && ctest --test-dir native/build-linux --output-on-failure' ;;
  ssh)
    "${RUN[@]}" "$IMG" bash -c '
      /usr/sbin/sshd -p 2222 -o PasswordAuthentication=yes -o KbdInteractiveAuthentication=no -o UsePAM=yes -o AllowUsers=winscp
      su winscp -c "cd ~ && echo hello > hello.txt && mkdir -p testdir && echo n > testdir/nested.txt"
      su dev -c "native/scripts/e2e-ssh.sh native/build-linux 127.0.0.1 2222 winscp winscp123"' ;;
  features)
    DATA="$(cd "${1:?data dir (servers.sh up)}" && pwd)"; shift
    "${RUN[@]}" -v "$DATA:$DATA" -e "E2E_SECTIONS=$*" -e "E2E_DATA_DIR=$DATA" "$IMG" su dev -c '
      set -u
      H=host.docker.internal
      for p in 2223 2224 21 2121 2990 8087 9101 $(seq 21000 21010) $(seq 21100 21110) $(seq 21200 21210); do
        socat "TCP-LISTEN:$p,fork,reuseaddr" "TCP:$H:$p" >/dev/null 2>&1 &
      done
      R=/tmp/e2e-run; mkdir -p "$R"; S=native/scripts/e2e
      python3 $S/proxy.py socks 11080 "$R/socks.log" > "$R/p1.out" 2>&1 &
      python3 $S/proxy.py socks 11081 "$R/socksauth.log" puser ppass > "$R/p2.out" 2>&1 &
      python3 $S/proxy.py http 13128 "$R/http.log" > "$R/p3.out" 2>&1 &
      python3 $S/proxy.py http 13129 "$R/httpauth.log" puser ppass > "$R/p4.out" 2>&1 &
      python3 $S/proxy.py http 13130 "$R/httpclose.log" puser ppass close > "$R/p5.out" 2>&1 &
      mkdir -p /tmp/kbroot; echo "hello 2fa" > /tmp/kbroot/hello.txt
      python3 $S/kbdint_sshd.py 2231 /tmp/kbroot twostep > "$R/k1.out" 2>&1 &
      python3 $S/kbdint_sshd.py 2232 /tmp/kbroot rounds  > "$R/k2.out" 2>&1 &
      python3 $S/kbdint_sshd.py 2233 /tmp/kbroot otponly > "$R/k3.out" 2>&1 &
      sleep 3
      # a private copy of the environment file: this container has its own address and helper logs
      D=/tmp/e2e-data; mkdir -p "$D"
      for e in "$E2E_DATA_DIR"/*; do ln -s "$e" "$D/$(basename "$e")"; done
      rm -f "$D/env.sh" "$D/run"; ln -s "$R" "$D/run"
      { echo "export E2E_DATA=$D"; echo "export E2E_LAN_IP=$(hostname -I | cut -d" " -f1)"; echo "export E2E_HAVE_KBDINT=1"; } > "$D/env.sh"
      native/scripts/e2e-features.sh native/build-linux "$D" $E2E_SECTIONS' ;;
  shell)
    "${RUN[@]}" "$IMG" su dev ;;
  *) echo "unknown command: $CMD"; exit 2 ;;
esac
