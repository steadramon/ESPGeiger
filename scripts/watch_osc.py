#!/usr/bin/env python3
# Stdlib-only watcher for the ESPGeiger UDP multicast feed.
# Joins the group, parses /espg/<chipid>/click and /stats messages
# (including OSC #bundle envelopes), prints each event with a timestamp.
#
# Usage:  ./watch_osc.py [group] [port]
# Defaults: 239.255.42.42 : 57340

import socket
import struct
import sys
import time

GROUP = sys.argv[1] if len(sys.argv) > 1 else "239.255.42.42"
PORT  = int(sys.argv[2]) if len(sys.argv) > 2 else 57340


def parse_osc_string(buf, off):
    """Read a NUL-terminated OSC string starting at off, padded to 4 bytes."""
    end = buf.find(b"\x00", off)
    if end < 0:
        return None, 0
    raw = buf[off:end].decode("ascii", errors="replace")
    pad = ((end - off + 1) + 3) & ~3
    return raw, off + pad


def parse_msg(buf):
    """Parse one /espg/<chipid>/{click|stats} message.

    Wire layout (path 20 bytes, then type tag + args):
      /click ,ii    counter(i32) ts_ms(i32)                          = 32 B
                    (tag ",ii\\0" padded to 4 bytes, args from off 24)
      /stats ,fffsii cpm(f32) usv(f32) hv(f32) state(str) rssi(i32)
                     uptime_s(i32)                                    = >= 56 B
                    (tag ",fffsii\\0" padded to 8 bytes, args from off 28)
    """
    if len(buf) < 24 or not buf.startswith(b"/espg/"):
        return None
    if buf[12:13] != b"/":
        return None
    chipid = buf[6:12].decode("ascii", errors="replace")
    suffix = buf[13:18].decode("ascii", errors="replace")
    if suffix == "click" and len(buf) >= 32:
        counter, ts_ms = struct.unpack(">II", buf[24:32])
        return ("click", chipid, counter, ts_ms)
    if suffix == "stats" and len(buf) >= 56:
        cpm, usv, hv = struct.unpack(">fff", buf[28:40])
        state, off = parse_osc_string(buf, 40)
        if state is None or off + 8 > len(buf):
            return ("stats", chipid, cpm, usv, hv, "?", 0, 0)
        rssi, uptime_s = struct.unpack(">ii", buf[off:off+8])
        return ("stats", chipid, cpm, usv, hv, state, rssi, uptime_s)
    return None


def format_msg(m):
    """Render a parsed tuple with labelled fields."""
    if m[0] == "click":
        _, chipid, counter, ts_ms = m
        return f"click  {chipid}  c={counter:<6}  ts={ts_ms}"
    if m[0] == "stats":
        _, chipid, cpm, usv, hv, state, rssi, uptime_s = m
        return (f"stats  {chipid}  cpm={cpm:6.2f}  usv={usv:.4f}  "
                f"hv={hv:.1f}  state={state:<8} rssi={rssi:>4}  up={uptime_s}")
    return " ".join(str(x) for x in m)


def parse_datagram(buf, src):
    """Top level: peel #bundle envelope or process a single message."""
    if buf.startswith(b"#bundle"):
        off = 16
        while off + 4 <= len(buf):
            sz = struct.unpack(">I", buf[off:off+4])[0]
            off += 4
            if sz == 0 or off + sz > len(buf):
                break
            m = parse_msg(buf[off:off + sz])
            if m:
                print(f"{time.strftime('%H:%M:%S')}  {src:<15}  {format_msg(m)}")
            off += sz
    else:
        m = parse_msg(buf)
        if m:
            print(f"{time.strftime('%H:%M:%S')}  {src:<15}  {format_msg(m)}")


def main():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("", PORT))
    mreq = struct.pack("4sL", socket.inet_aton(GROUP), socket.INADDR_ANY)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    print(f"Listening on {GROUP}:{PORT} (Ctrl+C to stop)")
    print("Fields:")
    print("  click  chipid  c=counter  ts=producer_millis")
    print("  stats  chipid  cpm=CPM  usv=uSv/h  hv=volts  state  rssi=dBm  up=uptime_s")
    print()
    try:
        while True:
            buf, addr = s.recvfrom(2048)
            parse_datagram(buf, addr[0])
    except KeyboardInterrupt:
        print()


if __name__ == "__main__":
    main()
