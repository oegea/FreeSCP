#!/usr/bin/env python3
"""Minimal logging proxy for the FreeSCP end-to-end tests (standard library only).

  proxy.py socks  <port> <logfile> [user password]    SOCKS4 / SOCKS5 (optional user/password auth)
  proxy.py http   <port> <logfile> [user password [close]]
                                                      HTTP CONNECT + plain-HTTP forwarding (optional Basic
                                                      auth; "close" = drop the connection after a 407, so
                                                      the client has to reconnect to retry with credentials)

Every accepted request appends one line to <logfile>: "<kind> <host>:<port> auth=<user|->", so a
test can prove the connection really went through the proxy. Listens on 127.0.0.1 only.
"""
import base64, select, socket, struct, sys, threading


def log(path, line):
    with open(path, "a") as f:
        f.write(line + "\n")


def recv_exact(s, n):
    data = b""
    while len(data) < n:
        chunk = s.recv(n - len(data))
        if not chunk:
            raise ConnectionError("eof")
        data += chunk
    return data


def pump(a, b):
    try:
        while True:
            r, _, _ = select.select([a, b], [], [], 300)
            if not r:
                break
            for s in r:
                data = s.recv(65536)
                if not data:
                    return
                (b if s is a else a).sendall(data)
    except OSError:
        pass
    finally:
        a.close()
        b.close()


def socks(client, logfile, cred):
    ver = recv_exact(client, 1)[0]
    if ver == 4:
        cmd, port = struct.unpack("!BH", recv_exact(client, 3))
        ip = recv_exact(client, 4)
        user = b""
        while True:
            c = recv_exact(client, 1)
            if c == b"\0":
                break
            user += c
        host = socket.inet_ntoa(ip)
        if ip[:3] == b"\0\0\0" and ip[3] != 0:      # SOCKS4a: host name follows
            name = b""
            while True:
                c = recv_exact(client, 1)
                if c == b"\0":
                    break
                name += c
            host = name.decode()
        log(logfile, "socks4 %s:%d auth=%s" % (host, port, user.decode() or "-"))
        try:
            upstream = socket.create_connection((host, port), 15)
        except OSError:
            client.sendall(b"\0\x5b" + b"\0" * 6)
            return
        client.sendall(b"\0\x5a" + b"\0" * 6)
        pump(client, upstream)
        return
    if ver != 5:
        return
    methods = recv_exact(client, recv_exact(client, 1)[0])
    user = "-"
    if cred:
        if 2 not in methods:
            client.sendall(b"\x05\xff")
            log(logfile, "socks5 REJECT no-auth-offered")
            return
        client.sendall(b"\x05\x02")
        recv_exact(client, 1)
        u = recv_exact(client, recv_exact(client, 1)[0]).decode()
        p = recv_exact(client, recv_exact(client, 1)[0]).decode()
        if (u, p) != cred:
            client.sendall(b"\x01\x01")
            log(logfile, "socks5 REJECT bad-credentials user=%s" % u)
            return
        client.sendall(b"\x01\x00")
        user = u
    else:
        client.sendall(b"\x05\x00")
    _, cmd, _, atyp = recv_exact(client, 4)
    if atyp == 1:
        host = socket.inet_ntoa(recv_exact(client, 4))
    elif atyp == 3:
        host = recv_exact(client, recv_exact(client, 1)[0]).decode()
    else:
        host = socket.inet_ntop(socket.AF_INET6, recv_exact(client, 16))
    port = struct.unpack("!H", recv_exact(client, 2))[0]
    log(logfile, "socks5 %s:%d auth=%s" % (host, port, user))
    try:
        upstream = socket.create_connection((host, port), 15)
    except OSError:
        client.sendall(b"\x05\x05\x00\x01" + b"\0" * 6)
        return
    client.sendall(b"\x05\x00\x00\x01" + b"\0" * 6)
    pump(client, upstream)


def http(client, logfile, cred, close_on_407=False):
    while True:                      # HTTP/1.1 keep-alive: a 407 may be followed by a retry on the same connection
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = client.recv(4096)
            if not chunk:
                return
            head += chunk
        lines = head.split(b"\r\n")
        parts = lines[0].decode().split()
        if len(parts) < 2:
            return
        user = "-"
        if cred:
            got = None
            for l in lines[1:]:
                if l.lower().startswith(b"proxy-authorization: basic "):
                    got = base64.b64decode(l.split(b" ", 2)[2]).decode()
            if got != "%s:%s" % cred:
                log(logfile, "http REJECT %s auth=%s" % (parts[1], (got or "-").split(":")[0]))
                # With an error document, as real proxies send (PuTTY's negotiator needs a byte
                # after the header block before it treats the headers as complete).
                body = b"<html><body>Proxy authentication required</body></html>\n"
                client.sendall(b"HTTP/1.1 407 Proxy Authentication Required\r\n"
                               b"Proxy-Authenticate: Basic realm=\"e2e\"\r\n" +
                               (b"Connection: close\r\n" if close_on_407 else b"") +
                               b"Content-Type: text/html\r\n" +
                               b"Content-Length: " + str(len(body)).encode() + b"\r\n\r\n" + body)
                if close_on_407:
                    return
                continue
            user = cred[0]
        if parts[0].upper() == "CONNECT":
            host, _, port = parts[1].rpartition(":")
            log(logfile, "http %s:%s auth=%s" % (host, port, user))
            try:
                upstream = socket.create_connection((host, int(port)), 15)
            except OSError:
                client.sendall(b"HTTP/1.1 502 Bad Gateway\r\n\r\n")
                return
            client.sendall(b"HTTP/1.1 200 Connection established\r\n\r\n")
            pump(client, upstream)
            return
        # Plain-HTTP forward proxying (what a WebDAV/S3 client does for http:// URLs): the request
        # line carries an absolute URI. Relay the request as received and then the raw byte stream.
        url = parts[1]
        if not url.lower().startswith("http://"):
            client.sendall(b"HTTP/1.1 400 Bad Request\r\n\r\n")
            return
        hostport = url[7:].split("/", 1)[0]
        host, _, port = hostport.partition(":")
        log(logfile, "http-forward %s:%s auth=%s" % (host, port or "80", user))
        try:
            upstream = socket.create_connection((host, int(port or 80)), 15)
        except OSError:
            client.sendall(b"HTTP/1.1 502 Bad Gateway\r\n\r\n")
            return
        upstream.sendall(head)
        pump(client, upstream)
        return


def main():
    kind, port, logfile = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    cred = (sys.argv[4], sys.argv[5]) if len(sys.argv) > 5 else None
    close_on_407 = len(sys.argv) > 6 and sys.argv[6] == "close"
    if kind == "socks":
        handler = socks
    else:
        def handler(c, logfile, cred):
            http(c, logfile, cred, close_on_407)
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))
    srv.listen(32)
    open(logfile, "a").close()
    print("proxy %s listening on 127.0.0.1:%d" % (kind, port), flush=True)

    def serve(c):
        try:
            handler(c, logfile, cred)
        except Exception:
            pass
        finally:
            try:
                c.close()
            except OSError:
                pass

    while True:
        c, _ = srv.accept()
        threading.Thread(target=serve, args=(c,), daemon=True).start()


if __name__ == "__main__":
    main()
