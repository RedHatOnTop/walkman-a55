"""Client for the wmprobe plugin.

    probe.py [--host H] [--port P] COMMAND [COMMAND ...]

Each COMMAND is one probe line (quote it), e.g.
    probe.py help "tree 3" "props 12"
With no commands, reads lines from stdin. On the device side, first run
`adb forward tcp:7777 tcp:7777`.
"""
import argparse
import socket
import sys

END = b"\n.\n"


def read_reply(sock, buf):
    while END not in buf:
        chunk = sock.recv(65536)
        if not chunk:
            raise ConnectionError("probe closed the connection")
        buf += chunk
    reply, _, rest = buf.partition(END)
    return reply.decode("utf-8", "replace"), rest


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=7777)
    ap.add_argument("--timeout", type=float, default=10.0)
    ap.add_argument("commands", nargs="*")
    args = ap.parse_args()

    commands = args.commands or [l.rstrip("\n") for l in sys.stdin if l.strip()]
    with socket.create_connection((args.host, args.port), timeout=args.timeout) as sock:
        banner, buf = read_reply(sock, b"")
        print(banner, file=sys.stderr)
        for cmd in commands:
            sock.sendall(cmd.encode() + b"\n")
            reply, buf = read_reply(sock, buf)
            print(f"> {cmd}")
            print(reply)


if __name__ == "__main__":
    main()
