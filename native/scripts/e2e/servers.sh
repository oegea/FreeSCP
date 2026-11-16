#!/usr/bin/env bash
# Test servers for native/scripts/e2e-features.sh.
#
#   servers.sh up   <data-dir>     start everything, write <data-dir>/env.sh
#   servers.sh down <data-dir>     stop everything (the data directory is kept)
#
# Docker containers (data bind-mounted under <data-dir>, so the Docker VM disk is not used):
#   freescp-e2e-sshd    OpenSSH, user winscp/winscp123, port 2223 (TCP forwarding OFF)
#   freescp-e2e-jump    OpenSSH, user jumper/jump456,   port 2224 (TCP forwarding ON: the jump host)
#   freescp-e2e-ftp     vsftpd,  user winscp/winscp123, port 21  (passive 21000-21010)
#   freescp-e2e-ftps    vsftpd,  explicit TLS, port 2121 (passive 21100-21110)
#   freescp-e2e-ftpsi   vsftpd,  implicit TLS, port 2990 (passive 21200-21210)
#   freescp-e2e-dav     Apache mod_dav, winscp/winscp123, port 8087
#   freescp-e2e-s3      MinIO, minioadmin/minioadmin123, port 9101, bucket "testbucket"
# All on the Docker network "freescp-e2e": the jump host reaches the others by container name,
# which is what the SSH tunnel tests use (the targets are then NOT reachable directly by that name).
#
# Host processes (Python 3):
#   proxy.py          SOCKS4/5 on 11080, SOCKS5 with auth (puser/ppass) on 11081, HTTP CONNECT on
#                     13128, HTTP with Basic auth on 13129 (keep-alive) and 13130 (closes after 407)
#   kbdint_sshd.py    keyboard-interactive / 2FA SSH servers on 2231 (two fields), 2232 (two
#                     rounds), 2233 (one-time code only). Needs `asyncssh` (a venv is created in
#                     <data-dir>/venv; without network access these tests are skipped).
set -uo pipefail
CMD="${1:-}"; D="${2:-}"
[ -n "$CMD" ] && [ -n "$D" ] || { echo "usage: $0 up|down <data-dir>"; exit 2; }
HERE="$(cd "$(dirname "$0")" && pwd)"
NAMES="freescp-e2e-sshd freescp-e2e-jump freescp-e2e-ftp freescp-e2e-ftps freescp-e2e-ftpsi freescp-e2e-dav freescp-e2e-s3"

stop_host_helpers() { pkill -f "$HERE/proxy.py" 2>/dev/null; pkill -f "$HERE/kbdint_sshd.py" 2>/dev/null; true; }

if [ "$CMD" = down ]; then
  stop_host_helpers
  for c in $NAMES; do docker rm -f "$c" >/dev/null 2>&1; done
  docker network rm freescp-e2e >/dev/null 2>&1
  echo "stopped"; exit 0
fi
[ "$CMD" = up ] || { echo "usage: $0 up|down <data-dir>"; exit 2; }

mkdir -p "$D"; D="$(cd "$D" && pwd)"
mkdir -p "$D"/{sshhome,jumphome,ftphome,ftpshome,ftpsihome,davdata,s3data/testbucket,tls,keys,kbroot,run}
chmod -R 777 "$D/davdata" "$D/ftphome" "$D/ftpshome" "$D/ftpsihome" 2>/dev/null

# keys + certificate
[ -f "$D/keys/k_plain" ]   || ssh-keygen -q -t ed25519 -N "" -f "$D/keys/k_plain" -C e2e-plain
[ -f "$D/keys/k_enc" ]     || ssh-keygen -q -t ed25519 -N "keypass" -f "$D/keys/k_enc" -C e2e-enc
[ -f "$D/keys/k_rsa_pem" ] || ssh-keygen -q -t rsa -b 2048 -m PEM -N "" -f "$D/keys/k_rsa_pem" -C e2e-rsa
[ -f "$D/tls/cert.pem" ]   || openssl req -x509 -newkey rsa:2048 -nodes -keyout "$D/tls/key.pem" -out "$D/tls/cert.pem" -days 365 -subj "/CN=localhost" 2>/dev/null
for h in sshhome jumphome; do
  mkdir -p "$D/$h/.ssh"; cat "$D"/keys/*.pub > "$D/$h/.ssh/authorized_keys"
  chmod 700 "$D/$h/.ssh"; chmod 600 "$D/$h/.ssh/authorized_keys"
done

docker network create freescp-e2e >/dev/null 2>&1
for c in $NAMES; do docker rm -f "$c" >/dev/null 2>&1; done
U="$(id -u)"; G="$(id -g)"
docker run -d --name freescp-e2e-sshd --network freescp-e2e -e PASSWORD_ACCESS=true -e USER_NAME=winscp -e USER_PASSWORD=winscp123 \
  -e PUID=$U -e PGID=$G -p 2223:2222 -v "$D/sshhome:/config" linuxserver/openssh-server >/dev/null
docker run -d --name freescp-e2e-jump --network freescp-e2e -e PASSWORD_ACCESS=true -e USER_NAME=jumper -e USER_PASSWORD=jump456 \
  -e PUID=$U -e PGID=$G -p 2224:2222 -v "$D/jumphome:/config" linuxserver/openssh-server >/dev/null
docker run -d --name freescp-e2e-ftp --network freescp-e2e -e 'USERS=winscp|winscp123|/home/winscp' -e ADDRESS=127.0.0.1 \
  -p 21:21 -p 21000-21010:21000-21010 -v "$D/ftphome:/home/winscp" delfer/alpine-ftp-server >/dev/null
docker run -d --name freescp-e2e-ftps -e 'USERS=winscp|winscp123|/home/winscp' -e ADDRESS=127.0.0.1 -e MIN_PORT=21100 -e MAX_PORT=21110 \
  -e TLS_CERT=/tls/cert.pem -e TLS_KEY=/tls/key.pem -p 2121:21 -p 21100-21110:21100-21110 \
  -v "$D/tls:/tls:ro" -v "$D/ftpshome:/home/winscp" delfer/alpine-ftp-server >/dev/null
docker run -d --name freescp-e2e-ftpsi -e 'USERS=winscp|winscp123|/home/winscp' -p 2990:990 -p 21200-21210:21200-21210 \
  -v "$D/tls:/tls:ro" -v "$D/ftpsihome:/home/winscp" delfer/alpine-ftp-server \
  vsftpd /etc/vsftpd/vsftpd.conf -obackground=NO -olisten_port=990 -oimplicit_ssl=YES -opasv_min_port=21200 -opasv_max_port=21210 \
  -opasv_address=127.0.0.1 -orsa_cert_file=/tls/cert.pem -orsa_private_key_file=/tls/key.pem -ossl_enable=YES -oallow_anon_ssl=NO \
  -oforce_local_data_ssl=YES -oforce_local_logins_ssl=YES -ossl_ciphers=HIGH >/dev/null
docker run -d --name freescp-e2e-dav --network freescp-e2e -e AUTH_TYPE=Basic -e USERNAME=winscp -e PASSWORD=winscp123 \
  -p 8087:80 -v "$D/davdata:/var/lib/dav" bytemark/webdav >/dev/null
# MinIO: the upstream image was withdrawn from the registries; use it if it is still cached locally,
# otherwise the archived Bitnami build of the same server.
chmod -R 777 "$D/s3data" 2>/dev/null
if docker image inspect minio/minio >/dev/null 2>&1; then
  docker run -d --name freescp-e2e-s3 --network freescp-e2e -e MINIO_ROOT_USER=minioadmin -e MINIO_ROOT_PASSWORD=minioadmin123 \
    -p 9101:9000 -v "$D/s3data:/data" minio/minio server /data >/dev/null
else
  docker run -d --name freescp-e2e-s3 --network freescp-e2e -e MINIO_ROOT_USER=minioadmin -e MINIO_ROOT_PASSWORD=minioadmin123 \
    -e MINIO_DEFAULT_BUCKETS=testbucket -p 9101:9000 -v "$D/s3data:/bitnami/minio/data" bitnamilegacy/minio >/dev/null
fi

# the jump host must allow forwarding (the image ships with it off); its config appears after first start
# (edited inside the container: on a Linux host the file belongs to the container's user)
for i in $(seq 1 30); do docker exec freescp-e2e-jump test -f /config/sshd/sshd_config 2>/dev/null && break; sleep 1; done
if docker exec freescp-e2e-jump grep -q '^AllowTcpForwarding no' /config/sshd/sshd_config 2>/dev/null; then
  docker exec freescp-e2e-jump sed -i 's/^AllowTcpForwarding no/AllowTcpForwarding yes/' /config/sshd/sshd_config
  docker restart freescp-e2e-jump >/dev/null
fi

# WebDAV root: the tests create directories in it from the host (a Linux host honours the owner)
for i in $(seq 1 20); do docker exec freescp-e2e-dav test -d /var/lib/dav/data 2>/dev/null && break; sleep 1; done
docker exec freescp-e2e-dav chmod 777 /var/lib/dav/data 2>/dev/null

# host helpers
stop_host_helpers
PY="$(command -v python3)"
for spec in "socks 11080 socks.log" "socks 11081 socksauth.log puser ppass" "http 13128 http.log" \
            "http 13129 httpauth.log puser ppass" "http 13130 httpclose.log puser ppass close"; do
  set -- $spec; kind=$1; port=$2; log=$3; shift 3
  : > "$D/run/$log"
  nohup "$PY" "$HERE/proxy.py" "$kind" "$port" "$D/run/$log" "$@" > "$D/run/proxy-$port.out" 2>&1 &
done
KBD=0
if [ ! -x "$D/venv/bin/python" ]; then "$PY" -m venv "$D/venv" >/dev/null 2>&1 && "$D/venv/bin/pip" install -q asyncssh >/dev/null 2>&1; fi
if "$D/venv/bin/python" -c "import asyncssh" >/dev/null 2>&1; then
  echo "hello 2fa" > "$D/kbroot/hello.txt"
  nohup "$D/venv/bin/python" "$HERE/kbdint_sshd.py" 2231 "$D/kbroot" twostep > "$D/run/kbd-2231.out" 2>&1 &
  nohup "$D/venv/bin/python" "$HERE/kbdint_sshd.py" 2232 "$D/kbroot" rounds  > "$D/run/kbd-2232.out" 2>&1 &
  nohup "$D/venv/bin/python" "$HERE/kbdint_sshd.py" 2233 "$D/kbroot" otponly > "$D/run/kbd-2233.out" 2>&1 &
  KBD=1
fi

# an address of this machine that is not "localhost": SFTP/SCP never send localhost through a proxy
LAN="$( (ipconfig getifaddr en0 || ipconfig getifaddr en1) 2>/dev/null || hostname -I 2>/dev/null | awk '{print $1}')"

cat > "$D/env.sh" <<EOF
# written by servers.sh — sourced by e2e-features.sh
export E2E_DATA="$D"
export E2E_LAN_IP="$LAN"
export E2E_HAVE_KBDINT=$KBD
EOF
sleep 6
docker ps --format '{{.Names}}  {{.Status}}' | grep freescp-e2e
echo "data: $D   lan ip: ${LAN:-none}   2FA servers: $([ $KBD = 1 ] && echo yes || echo 'no (asyncssh not available)')"
