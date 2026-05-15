#!/usr/bin/env python3
# Stdlib-only watcher for the ESPGeiger UDP multicast feed.
# Joins the group, parses /espg/<chipid>/{click,rad,sys} messages,
# prints each event with a timestamp.
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
    """Parse one /espg/<chipid>/{click|rad|sys} message.

    All paths pad to 20 bytes. Tag follows; args after the tag's 4-byte pad.
      /click ,ii    counter(i32) ts_ms(i32)
      /rad   ,fffsi cpm usv hv state(str) total_clicks(i32)
      /sys   ,iiiiii uptime_s rssi free_heap heap_frag_pct lps tick_max_us
    """
    if len(buf) < 24 or not buf.startswith(b"/espg/"):
        return None
    if buf[12:13] != b"/":
        return None
    chipid = buf[6:12].decode("ascii", errors="replace")
    s0 = buf[13:14]
    if s0 == b"c" and buf[13:18] == b"click" and len(buf) >= 32:
        counter, ts_ms = struct.unpack(">II", buf[24:32])
        return ("click", chipid, counter, ts_ms)
    if s0 == b"r" and buf[13:16] == b"rad" and len(buf) >= 48:
        # /rad path is 16 chars + NUL, padded to 20. Tag ",fffsi\0" pads to 8.
        cpm, usv, hv = struct.unpack(">fff", buf[28:40])
        state, off = parse_osc_string(buf, 40)
        if state is None or off + 4 > len(buf):
            return ("rad", chipid, cpm, usv, hv, "?", 0)
        (total,) = struct.unpack(">I", buf[off:off+4])
        return ("rad", chipid, cpm, usv, hv, state, total)
    if s0 == b"s" and buf[13:16] == b"sys" and len(buf) >= 52:
        # Tag ",iiiiii\0" pads to 8, args at off 28.
        uptime_s, rssi, free_heap, frag, lps, tick_max_us = struct.unpack(
            ">iiiiii", buf[28:52]
        )
        return ("sys", chipid, uptime_s, rssi, free_heap, frag, lps, tick_max_us)
    return None


def format_msg(m):
    """Render a parsed tuple with labelled fields."""
    if m[0] == "click":
        _, chipid, counter, ts_ms = m
        return f"click  {chipid}  c={counter:<6}  ts={ts_ms}"
    if m[0] == "rad":
        _, chipid, cpm, usv, hv, state, total = m
        return (f"rad    {chipid}  cpm={cpm:6.2f}  usv={usv:.4f}  "
                f"hv={hv:.1f}  state={state:<8} total={total}")
    if m[0] == "sys":
        _, chipid, uptime_s, rssi, free_heap, frag, lps, tick_max_us = m
        return (f"sys    {chipid}  up={uptime_s:<6}  rssi={rssi:>4}  "
                f"free={free_heap}  frag={frag}%  lps={lps}  tickmax={tick_max_us}us")
    return " ".join(str(x) for x in m)


def parse_datagram(buf, src):
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
    print("  rad    chipid  cpm=CPM  usv=uSv/h  hv=volts  state  total=lifetime_clicks")
    print("  sys    chipid  up=uptime_s  rssi=dBm  free=heap_B  frag=%  lps  tickmax=us")
    print()
    try:
        while True:
            buf, addr = s.recvfrom(2048)
            parse_datagram(buf, addr[0])
    except KeyboardInterrupt:
        print()


if __name__ == "__main__":
    main()
