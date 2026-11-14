#!/usr/bin/env python3
# Corpus firehose over loopback: BEGIN marker, corpus x repeats, END marker, then counts the
# newline-terminated lines the client writes back (CLIENT_LINES; svof must send none).
# Binds a free port and writes it to <portfile>. With --go the flood waits for that file.
# usage: logserver.py <corpus> <repeats> <log> <portfile> [--go <file>] [--readback <s>]
import argparse
import os
import socket
import time

ap = argparse.ArgumentParser()
ap.add_argument("corpus")
ap.add_argument("repeats", type=int)
ap.add_argument("log")
ap.add_argument("portfile")
ap.add_argument("--go", default="")
ap.add_argument("--readback", type=int, default=600)
a = ap.parse_args()

logf = open(a.log, "w", buffering=1)


def note(m):
    t = time.time()
    logf.write(time.strftime("%H:%M:%S", time.localtime(t)) + ".%03d %s\n" % (int(t * 1000) % 1000, m))


data = open(a.corpus, "rb").read()
srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", 0))
srv.listen(1)
port = srv.getsockname()[1]
with open(a.portfile + ".tmp", "w") as f:
    f.write(str(port))
os.rename(a.portfile + ".tmp", a.portfile)
note("listening on %d, %d bytes x %d" % (port, len(data), a.repeats))
srv.settimeout(600)
c, _ = srv.accept()
c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
note("client connected")
if a.go:
    for _ in range(1200):
        if os.path.exists(a.go):
            break
        time.sleep(0.05)
# Telnet negotiation the client sent before the flood is not a line it wrote
c.setblocking(False)
try:
    while c.recv(65536):
        pass
except (BlockingIOError, OSError):
    pass
c.setblocking(True)
t0 = time.time()
c.sendall(b"--- BEGIN MEASURING ---\r\n")
for _ in range(a.repeats):
    c.sendall(data)
c.sendall(b"--- END MEASURING ---\r\n")
dt = time.time() - t0
total = len(data) * a.repeats
note("SENT %d bytes in %.1f ms (%.1f MiB/s)" % (total, dt * 1000, total / max(dt, 1e-9) / 1048576))

c.settimeout(1)
buf = b""
lines = logged = idle = 0
closed = False
while idle < a.readback:
    try:
        d = c.recv(65536)
    except socket.timeout:
        idle += 1
        continue
    except OSError:
        closed = True
        break
    if not d:
        closed = True
        break
    idle = 0
    buf += d
    while b"\n" in buf:
        line, buf = buf.split(b"\n", 1)
        clean = bytes(ch for ch in line if 32 <= ch <= 126).strip()
        if clean:
            lines += 1
            if logged < 60:
                logged += 1
                note("CLIENT> " + clean.decode("ascii"))
            note("CLIENT_LINES %d" % lines)
if closed:
    note("client closed")
note("CLIENT_LINES %d" % lines)
c.close()
srv.close()
note("done")
